package com.hexohm.crsminerandroid;

import android.app.*;
import android.content.*;
import android.net.nsd.*;
import android.os.*;
import org.json.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** Explicitly user-started, foreground SHA-256d mining service. Never starts on boot. */
public final class MiningService extends Service {
    static final String ACTION_START="com.hexohm.crsminerandroid.START";
    static final String ACTION_STOP="com.hexohm.crsminerandroid.STOP";
    static final int HTTP_PORT=8080;
    private static final int NOTIFICATION_ID=54001;
    private static final int BATCH=32768;
    private static final long NONCES=0x100000000L;
    private static final String CHANNEL="mining";
    private static volatile MiningService instance;

    volatile boolean running=false, poolReady=false, poolConnected=false, authDenied=false, authorized=false;
    private volatile long workReadySince=0, disconnectedSince=0, authDeniedSince=0;
    volatile String stage="Stopped", error="", poolHost="", lastSubmit="", lastReply="", lastAuth="", ip="";
    volatile double poolDifficulty=1.0, rate=0, best=0;
    volatile long accepted=0,rejected=0,jobs=0, started=0;
    private volatile Job currentJob;
    private volatile byte[] currentTarget=Hex.target(1);
    private final AtomicLong seq=new AtomicLong(), en2counter=new AtomicLong(), hashes=new AtomicLong();
    private AtomicLongArray threadHashes=new AtomicLongArray(1);
    private volatile double[] coreRates=new double[]{0};
    private final BlockingQueue<String> submissions=new LinkedBlockingQueue<>(128);
    private final ArrayList<Thread> workers=new ArrayList<>();
    private Thread stratum,telemetry,web;
    private volatile Socket socket;
    private ServerSocket webSocket;
    private Config cfg;
    private NsdManager nsd;
    private NsdManager.RegistrationListener nsdListener;
    private PowerManager power;
    private PowerManager.WakeLock miningWakeLock;
    private NotificationManager notifs;
    private volatile boolean nativeOk=false;
    private int submitId=100;

    static boolean isRunning(){MiningService s=instance;return s!=null && s.running;}
    public static JSONObject snapshot(Context context) {
        MiningService s=instance;
        return s==null?stopped(context):s.status();
    }
    private static JSONObject stopped(Context ctx) {
        Config c=Config.load(ctx);JSONObject j=new JSONObject();
        try {j.put("hr",0);j.put("acc",0);j.put("rej",0);j.put("ok",false);j.put("host",c.hostname()+".local");j.put("stage","stopped");j.put("mode","Android SHA-256d CPU");}catch(JSONException ignored){}
        return j;
    }
    @Override public void onCreate(){super.onCreate();instance=this;power=(PowerManager)getSystemService(POWER_SERVICE);
        notifs=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        notifs.createNotificationChannel(new NotificationChannel(CHANNEL,"CRSminer mining",NotificationManager.IMPORTANCE_LOW));}
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        if(intent!=null && ACTION_STOP.equals(intent.getAction())){shutdown();stopSelf();return START_NOT_STICKY;}
        if(!running){
            cfg=Config.load(this);
            try {cfg.validate();nativeOk=NativeHash.selfTest();if(!nativeOk)throw new IllegalStateException("SHA-256d self-test failed");}
            catch(Exception e){error=e.getMessage();stage="Configuration/self-test error";MinerWidget.updateAll(this);stopSelf();return START_NOT_STICKY;}
            startForeground(NOTIFICATION_ID,notification());
            running=true;stage="Connecting";started=SystemClock.elapsedRealtime();
            // Screen-off CPU hashing: held only during user-started mining; released on Stop.
            if(power!=null){
                try{
                    miningWakeLock=power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"CRSminer:Mining");
                    miningWakeLock.setReferenceCounted(false);
                    miningWakeLock.acquire();
                }catch(SecurityException ex){error="CPU wake lock unavailable: "+ex.getMessage();}
            }
            MinerWidget.updateAll(this);
            accepted=0;rejected=0;jobs=0;best=0;rate=0;poolDifficulty=1;
            poolConnected=false;authDenied=false;authorized=false;workReadySince=0;disconnectedSince=started;authDeniedSince=0;
            error="";lastSubmit="";lastReply="";lastAuth="";
            hashes.set(0);threadHashes=new AtomicLongArray(cfg.threads);coreRates=new double[cfg.threads];submissions.clear();
            startHttp();registerNsd();
            telemetry=new Thread(this::statsLoop,"CRS-stats");telemetry.start();
            stratum=new Thread(this::stratumLoop,"CRS-stratum");stratum.start();
            for(int i=0;i<cfg.threads;i++){final int id=i;Thread t=new Thread(()->hashLoop(id),"CRS-CPU-"+i);workers.add(t);t.start();}
        }
        return START_NOT_STICKY;
    }
    private Notification notification(){
        Intent open=new Intent(this,MainActivity.class);
        PendingIntent p=PendingIntent.getActivity(this,0,open,PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Intent stop=new Intent(this,MiningService.class).setAction(ACTION_STOP);
        PendingIntent ps=PendingIntent.getService(this,1,stop,PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this,CHANNEL).setContentTitle("CRSminer for Android · HexOhm")
                .setContentText("Mining enabled · "+stage).setSmallIcon(android.R.drawable.stat_sys_download)
                .setOngoing(true).setContentIntent(p).addAction(android.R.drawable.ic_media_pause,"Stop mining",ps).build();
    }
    private void statsLoop(){long previous=0;long[] previousThread=new long[threadHashes.length()];
        int widgetTicks=0;
        while(running){try{Thread.sleep(2000);}catch(InterruptedException e){break;}
            long now=hashes.get();rate=Math.max(0,(now-previous)/2.0);previous=now;
            double[] rates=new double[threadHashes.length()];
            for(int i=0;i<rates.length;i++){long count=threadHashes.get(i);rates[i]=Math.max(0,(count-previousThread[i])/2.0);previousThread[i]=count;}
            coreRates=rates;
            notifs.notify(NOTIFICATION_ID,notification());
            if(++widgetTicks>=5){widgetTicks=0;MinerWidget.updateAll(this);}
        }
    }
    private void hashLoop(int coreId){
        while(running){
            Job j=currentJob;
            if(!poolReady || j==null || !nativeOk){nap(150);continue;}
            if(power!=null && Build.VERSION.SDK_INT>=29 && power.getCurrentThermalStatus()>=PowerManager.THERMAL_STATUS_SEVERE){
                stage="Paused: device too hot";nap(2500);continue;
            }
            if(stage.startsWith("Paused"))stage="mining";
            // Allocate unique extranonce2 values across all threads.
            long en=en2counter.getAndIncrement();
            byte[] h;
            try{h=j.header76(en);}catch(RuntimeException e){error="Malformed job: "+e.getMessage();nap(1000);continue;}
            byte[] target=currentTarget;
            for(long nonce=0;running && poolReady && j==currentJob && nonce<NONCES;nonce+=BATCH){
                if(power!=null && Build.VERSION.SDK_INT>=29 && power.getCurrentThermalStatus()>=PowerManager.THERMAL_STATUS_SEVERE)break;
                int count=(int)Math.min(BATCH,NONCES-nonce);
                long found=NativeHash.scan(h,target,nonce,count);
                long done=found>=0?found-nonce+1:count;hashes.addAndGet(done);threadHashes.addAndGet(coreId,done);
                if(found>=0){
                    // Verify in Java independently before submitting; failed native hits are never sent.
                    byte[] full=Hex.concat(h,Hex.decode(Hex.leHex(found,4)));
                    byte[] dg=Hex.sha2(full);
                    boolean realShare=Hex.littleHashUnderTarget(dg,target);
                    boolean nearMiss=dg[31]==0 && (dg[30]&0xf0)==0;
                    if(realShare || nearMiss) {
                        // Unlike v0.1, display best difficulty even if no pool-valid share is found.
                        best=Math.max(best,Hex.difficulty(dg));
                        if(realShare && j==currentJob && poolReady){
                            try {JSONObject submit=new JSONObject();submit.put("id",submitIdAtomic.getAndIncrement());
                                submit.put("method","mining.submit");
                                submit.put("params",new JSONArray().put(cfg.wallet+"."+cfg.worker).put(j.id)
                                       .put(j.extra2Hex(en)).put(j.ntime).put(Hex.netHex(found)));
                                submissions.offer(submit.toString());
                            }catch(JSONException ignored){}
                        }
                    }else error="Native candidate failed independent SHA verification";
                }
            }
        }
    }
    private final AtomicInteger submitIdAtomic=new AtomicInteger(100);
    private void stratumLoop(){
        while(running){
            String endpoint=cfg.endpoint();
            int colon=endpoint.lastIndexOf(':');String host=endpoint.substring(0,colon);
            int port=Integer.parseInt(endpoint.substring(colon+1));poolHost=host;
            currentJob=null;poolReady=false;poolConnected=false;authorized=false;workReadySince=0;submissions.clear();
            byte[] extranonce1=null;int extranonce2Size=4;
            double nextDifficulty=1.0;
            stage="Connecting to pool";
            try(Socket s=new Socket()){
                s.connect(new InetSocketAddress(host,port),7000);socket=s;s.setTcpNoDelay(true);s.setSoTimeout(750);
                poolConnected=true;disconnectedSince=0;error="";lastAuth="";
                BufferedReader in=new BufferedReader(new InputStreamReader(s.getInputStream(),StandardCharsets.UTF_8));
                BufferedWriter out=new BufferedWriter(new OutputStreamWriter(s.getOutputStream(),StandardCharsets.UTF_8));
                send(out,new JSONObject().put("id",1).put("method","mining.subscribe").put("params",new JSONArray().put("CRSminer-Android/0.1")));
                stage="Subscribing";long lastRx=SystemClock.elapsedRealtime();
                while(running && !s.isClosed()){
                    String pending;
                    while((pending=submissions.poll())!=null){out.write(pending);out.write('\n');lastSubmit=pending;}
                    out.flush();
                    String line;
                    try{line=in.readLine();}catch(SocketTimeoutException ex){
                        if(SystemClock.elapsedRealtime()-lastRx>600000)break;continue;
                    }
                    if(line==null)break;lastRx=SystemClock.elapsedRealtime();
                    if(line.length()>200000)throw new IOException("Oversized Stratum message");
                    try{
                        JSONObject message=new JSONObject(line);
                        String method=message.optString("method","");
                        if(method.equals("mining.set_difficulty")){
                            double diff=message.getJSONArray("params").getDouble(0);
                            if(diff>0 && Double.isFinite(diff))nextDifficulty=diff;
                        }else if(method.equals("mining.set_extranonce")){
                            JSONArray p=message.getJSONArray("params");extranonce1=Hex.decode(p.getString(0));extranonce2Size=p.getInt(1);
                            currentJob=null;poolReady=false;
                        }else if(method.equals("mining.notify")){
                            if(extranonce1==null)continue;
                            Job j=new Job(message.getJSONArray("params"),extranonce1,extranonce2Size,seq.incrementAndGet());
                            currentTarget=Hex.target(nextDifficulty);
                            poolDifficulty=nextDifficulty;en2counter.set(0);
                            // Clean-job notifications invalidate pending shares from old jobs.
                            if(message.getJSONArray("params").optBoolean(8,false))submissions.clear();
                            currentJob=j;poolReady=authorized;workReadySince=poolReady?SystemClock.elapsedRealtime():0;
                            jobs++;stage=poolReady?"mining":"Waiting for authorization";
                        }else if(message.has("id")&&!message.isNull("id")){
                            int id=message.optInt("id",-1);
                            if(id>=1 && id<=3)lastAuth=(lastAuth+" #"+id+":"+line).substring(Math.max(0,(lastAuth+" #"+id+":"+line).length()-350));
                            if(id==1){
                                JSONArray result=message.optJSONArray("result");
                                if(result==null)throw new IOException("Subscribe rejected: "+message.optString("error"));
                                extranonce1=Hex.decode(result.getString(1));extranonce2Size=result.getInt(2);
                                send(out,new JSONObject().put("id",2).put("method","mining.authorize")
                                     .put("params",new JSONArray().put(cfg.wallet+"."+cfg.worker).put(cfg.password)));
                                if(!cfg.suggested.isEmpty())send(out,new JSONObject().put("id",3)
                                     .put("method","mining.suggest_difficulty").put("params",new JSONArray().put(Double.parseDouble(cfg.suggested))));
                                stage="Authorizing";
                            }else if(id==2){
                                if(message.optBoolean("result",false)){
                                    authDenied=false;authDeniedSince=0;authorized=true;
                                    poolReady=currentJob!=null;
                                    workReadySince=poolReady?SystemClock.elapsedRealtime():0;
                                    stage=poolReady?"mining":"Waiting for job";
                                }else {authDenied=true;authorized=false;
                                    if(authDeniedSince==0)authDeniedSince=SystemClock.elapsedRealtime();
                                    error="Pool authorization refused: "+message.opt("error");throw new IOException(error);}
                            }else if(id>=100){
                                lastReply=line;
                                if(message.optBoolean("result",false))accepted++;
                                else {rejected++;error="Share rejected: "+message.opt("error");}
                            }
                        }
                    }catch(JSONException|IllegalArgumentException e){error="Malformed Stratum data: "+e.getMessage();}
                }
            }catch(IOException|JSONException e){if(running)error=e.getMessage();}
            finally{poolReady=false;currentJob=null;socket=null;poolConnected=false;authorized=false;
                workReadySince=0;disconnectedSince=SystemClock.elapsedRealtime();}
            if(running){stage="Reconnecting";nap(3000);}
        }
    }
    private static void send(BufferedWriter out,JSONObject v)throws IOException {out.write(v.toString());out.write('\n');out.flush();}
    private static void nap(long millis){try{Thread.sleep(millis);}catch(InterruptedException ignored){Thread.currentThread().interrupt();}}
    private void startHttp(){
        web=new Thread(()->{
            try(ServerSocket ss=new ServerSocket(HTTP_PORT,12)){webSocket=ss;ss.setSoTimeout(1000);
                while(running){try{Socket client=ss.accept();client.setSoTimeout(1700);handleHttp(client);}catch(SocketTimeoutException ignored){}catch(IOException ignored){}}
            }catch(IOException e){error="LAN HTTP server: "+e.getMessage();}
        },"CRS-LAN");web.start();
    }
    private void handleHttp(Socket client){
        try(Socket c=client){
            BufferedReader r=new BufferedReader(new InputStreamReader(c.getInputStream(),StandardCharsets.US_ASCII));
            String request=r.readLine();if(request==null||request.length()>4096)return;
            String[] v=request.split(" ");String path=v.length>1?v[1]:"";
            for(int i=0;i<60;i++){String line=r.readLine();if(line==null||line.isEmpty())break;}
            int code=200;String body;
            if(!request.startsWith("GET ")){code=405;body="{\"error\":\"Read-only endpoint\"}";}
            else if(path.equals("/api/status"))body=status().toString();
            else if(path.equals("/api/config"))body=configJson().toString();
            else if(path.equals("/")){body="<html><body style='background:#10141d;color:#dfc37c;font-family:sans-serif'><h2>CRSminer for Android</h2><p>by HexOhm</p><p>API: <a href='/api/status'>/api/status</a></p><p><a href='/license'>MIT License</a></p></body></html>";}
            else if(path.equals("/license")){body="<!doctype html><html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>CRSminer - MIT License</title><body style=\"background:#10141d;color:#dfc37c;font-family:sans-serif;padding:16px\"><h2>CRSminer for Android \u2014 MIT License</h2><pre style=\"white-space:pre-wrap\">CRSminer for Android \u2014 Copyright (c) 2026 HexOhm\nReleased under the MIT License. This Android source incorporates compatible concepts from the HexOhm CRSminer firmware.\n\nPermission is hereby granted, free of charge, to any person obtaining a copy\nof this software and associated documentation files (the &quot;Software&quot;), to deal\nin the Software without restriction, including without limitation the rights\nto use, copy, modify, merge, publish, distribute, sublicense, and/or sell\ncopies of the Software, and to permit persons to whom the Software is\nfurnished to do so, subject to the following conditions:\n\nThe above copyright notice and this permission notice shall be included in all\ncopies or substantial portions of the Software.\n\nTHE SOFTWARE IS PROVIDED &quot;AS IS&quot;, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\nIMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\nFITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\nAUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\nLIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\nOUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\nSOFTWARE.\n</pre><a style=\"color:#dfc37c\" href=\"/\">Back</a></body></html>";}
            else {code=404;body="{\"error\":\"Not found\"}";}
            byte[] bytes=body.getBytes(StandardCharsets.UTF_8);
            OutputStream o=c.getOutputStream();String type=(path.equals("/")||path.equals("/license"))?"text/html":"application/json";
            o.write(("HTTP/1.1 "+code+" "+(code==200?"OK":"Error")+"\r\nContent-Type: "+type+"; charset=utf-8\r\n"+
                    "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: "+bytes.length+"\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            o.write(bytes);o.flush();
        }catch(IOException ignored){}
    }
    private JSONObject configJson(){JSONObject j=new JSONObject();
        try{j.put("name",cfg.name);j.put("pool",cfg.endpoint());j.put("addr",cfg.wallet);j.put("worker",cfg.worker);
            j.put("sugg",cfg.suggested);j.put("platform","android");j.put("solo",cfg.solo);j.put("threads",cfg.threads);}catch(JSONException ignored){}
        return j;
    }
    JSONObject status(){JSONObject j=new JSONObject();
        try {
            String address=localIp();
            j.put("hr",rate);JSONArray cores=new JSONArray();for(double r:coreRates)cores.put(r);j.put("cores",cores);
            j.put("acc",accepted);j.put("rej",rejected);j.put("best",best);
            j.put("pdiff",poolDifficulty);j.put("up",started>0?(SystemClock.elapsedRealtime()-started)/1000:0);
            j.put("ok",poolReady);j.put("pool",poolHost);j.put("rssi",0);j.put("heap",Runtime.getRuntime().freeMemory());
            j.put("temp",JSONObject.NULL);j.put("jobs",jobs);j.put("host",cfg.hostname()+".local");
            j.put("mac","");j.put("ip",address);j.put("stage",stage);
            j.put("mode","Android CPU SHA-256d "+(cfg.solo?"solo-pool":"pool"));
            j.put("platform","android");j.put("http_port",HTTP_PORT);
            j.put("diag",diagnostic());
            j.put("sub",lastSubmit);j.put("rep",lastReply);j.put("auth",lastAuth);j.put("err",error);
        }catch(JSONException ignored){}
        return j;
    }
    // Match the ESP's delayed, actionable diagnostic alerts. "err" remains historical.
    // This avoids treating a historical "job not found" share rejection as permanent attention.
    private String diagnostic(){
        if(!running)return "";
        long now=SystemClock.elapsedRealtime();
        if(!nativeOk)return "SHA self-test failed";
        if(Build.VERSION.SDK_INT>=29 && power!=null
                && power.getCurrentThermalStatus()>=PowerManager.THERMAL_STATUS_SEVERE)return "Device hot: hashing paused";
        if(authDenied && authDeniedSince>0 && now-authDeniedSince>3000)return "pool refused the login (check wallet address)";
        if(!poolConnected && disconnectedSince>0 && now-disconnectedSince>15000)return "pool unreachable";
        if(poolReady && workReadySince>0){
            if(rate<1000 && now-workReadySince>30000)return "no hashing";
            if(rejected>=10 && rejected>accepted)return "most shares are rejected";
            if(accepted==0 && rate>0 && poolDifficulty>0){
                double etaMs=poolDifficulty*4294967296.0/rate*1000.0;
                double limit=Math.max(900000.0,etaMs*8.0);
                if(now-workReadySince>limit)return "no accepted shares for a long time";
            }
        }
        return "";
    }
    private String localIp(){
        try {Enumeration<NetworkInterface> interfaces=NetworkInterface.getNetworkInterfaces();
            while(interfaces.hasMoreElements()){NetworkInterface n=interfaces.nextElement();
                if(!n.isUp()||n.isLoopback())continue;
                Enumeration<InetAddress> addrs=n.getInetAddresses();
                while(addrs.hasMoreElements()){InetAddress a=addrs.nextElement();
                    if(a instanceof Inet4Address && a.isSiteLocalAddress())return a.getHostAddress();
                }
            }
        }catch(SocketException ignored){}return "";
    }
    private void registerNsd(){
        try{nsd=(NsdManager)getSystemService(NSD_SERVICE);NsdServiceInfo info=new NsdServiceInfo();
            info.setServiceName(cfg.hostname());info.setServiceType("_http._tcp.");info.setPort(HTTP_PORT);
            nsdListener=new NsdManager.RegistrationListener(){
                @Override public void onServiceRegistered(NsdServiceInfo i){}
                @Override public void onRegistrationFailed(NsdServiceInfo i,int code){error="mDNS advertisement failed ("+code+")";}
                @Override public void onServiceUnregistered(NsdServiceInfo i){}
                @Override public void onUnregistrationFailed(NsdServiceInfo i,int code){}
            };
            nsd.registerService(info,NsdManager.PROTOCOL_DNS_SD,nsdListener);
        }catch(RuntimeException e){error="mDNS registration: "+e.getMessage();}
    }
    private void shutdown(){
        if(!running)return;
        running=false;poolReady=false;poolConnected=false;authorized=false;currentJob=null;stage="Stopped";
        if(socket!=null)try{socket.close();}catch(IOException ignored){}
        if(webSocket!=null)try{webSocket.close();}catch(IOException ignored){}
        if(nsd!=null && nsdListener!=null)try{nsd.unregisterService(nsdListener);}catch(RuntimeException ignored){}
        if(telemetry!=null)telemetry.interrupt();if(stratum!=null)stratum.interrupt();
        for(Thread w:workers)w.interrupt();workers.clear();
        stopForeground(STOP_FOREGROUND_REMOVE);
        MinerWidget.updateAll(this);
    }
    @Override public void onDestroy(){shutdown();instance=null;MinerWidget.updateAll(this);super.onDestroy();}
    @Override public IBinder onBind(Intent intent){return null;}
}
