package com.hexohm.crsminerandroid;

import android.Manifest;
import android.app.Activity;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.*;
import android.view.*;
import android.widget.*;
import org.json.JSONObject;
import java.util.Locale;

/** Built without third-party UI dependencies, with familiar HexOhm dark / warm-gold accents. */
public final class MainActivity extends Activity {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private EditText name,pool,wallet,worker,password,suggested,threads;
    private Spinner mode;
    private TextView info,status,hashrate,shares,difficulty,details;
    private Button start,stop,save;
    private static final int BG=Color.rgb(11,16,25), PANEL=Color.rgb(22,31,44), GOLD=Color.rgb(219,180,91), WHITE=Color.rgb(230,235,244);
    private Config cfg;
    private boolean open=true;
    private final Runnable ticker=new Runnable(){@Override public void run(){if(!open)return;refresh();handler.postDelayed(this,1500);}};
    @Override public void onCreate(Bundle state){super.onCreate(state);getWindow().setNavigationBarColor(BG);cfg=Config.load(this);
        build();
        // Defer the status-bar request until the decor view has been created/attached.
        getWindow().getDecorView().post(this::hideStatusBar);
        handler.post(ticker);
        if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},10);
    }
    // Hide only the top status bar. Keep the navigation bar and software keyboard available.
    private void hideStatusBar(){
        // Request insets from the decor view, not from PhoneWindow before it exists.
        View decor=getWindow().getDecorView();
        if(Build.VERSION.SDK_INT>=30){
            WindowInsetsController controller=decor.getWindowInsetsController();
            if(controller!=null)controller.hide(WindowInsets.Type.statusBars());
        }else{
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(focus)hideStatusBar();}
    private TextView text(String value,int sp,int color,boolean bold){TextView t=new TextView(this);t.setText(value);t.setTextSize(sp);t.setTextColor(color);
        t.setPadding(0,6,0,6);if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);return t;}
    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private void heading(LinearLayout root,String label){root.addView(text(label,18,GOLD,true));}
    private EditText edit(LinearLayout root,String label,String value,String hint){
        root.addView(text(label,13,0xffaab9c9,false));
        EditText e=new EditText(this);
        e.setSingleLine(true);
        e.setText(value);
        e.setHint(hint);
        e.setTextColor(WHITE);
        e.setHintTextColor(0xff7f90a1);
        e.setTextSize(16);
        e.setIncludeFontPadding(true);
        e.setGravity(Gravity.CENTER_VERTICAL|Gravity.START);
        e.setMinHeight(dip(56));
        e.setPadding(dip(12),dip(14),dip(12),dip(10));
        e.setBackgroundTintList(android.content.res.ColorStateList.valueOf(GOLD));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);
        root.addView(e,lp);
        return e;
    }
    private Button button(String caption){Button b=new Button(this);b.setText(caption);b.setAllCaps(false);b.setTextColor(BG);b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(GOLD));return b;}
    private void build(){
        ScrollView sc=new ScrollView(this);sc.setFillViewport(true);sc.setBackgroundColor(BG);
        LinearLayout base=column();int p=dip(17);base.setPadding(p,dip(15),p,dip(25));sc.addView(base);setContentView(sc);
        TextView brand=text("⬡  HexOhm",15,GOLD,true);base.addView(brand);
        base.addView(text("CRSminer for Android",26,WHITE,true));
        base.addView(text("Real SHA-256d CPU mining · experimental build",12,0xffb6c3d0,false));
        base.addView(text("Separate app from CRS Mining monitor. Starts only when you press Start.",12,0xff889cad,false));

        LinearLayout hero=column();hero.setPadding(18,14,18,14);hero.setBackgroundColor(PANEL);
        base.addView(hero,new LinearLayout.LayoutParams(-1,-2));
        status=text("Stopped",15,GOLD,true);hero.addView(status);
        hashrate=text("0 H/s",32,WHITE,true);hero.addView(hashrate);
        shares=text("Accepted 0   •   Rejected 0",14,WHITE,false);hero.addView(shares);
        difficulty=text("Pool diff —   •   Best —",13,0xffabb9ca,false);hero.addView(difficulty);
        details=text("",12,0xffb1bfce,false);hero.addView(details);

        LinearLayout controls=new LinearLayout(this);controls.setOrientation(LinearLayout.HORIZONTAL);
        start=button("▶ Start mining");stop=button("■ Stop");
        controls.addView(start,new LinearLayout.LayoutParams(0,dip(58),2));controls.addView(stop,new LinearLayout.LayoutParams(0,dip(58),1));
        base.addView(controls);start.setOnClickListener(v->start());stop.setOnClickListener(v->stop());

        heading(base,"Miner configuration");
        name=edit(base,"Miner ID (4 letters or digits)",cfg.name,"a001");
        worker=edit(base,"Worker name",cfg.worker,"crs");
        base.addView(text("Mining mode",13,0xffaab9c9,false));
        mode=new Spinner(this);
        ArrayAdapter<String> a=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Pool mining","Solo / lottery mining via solo.ckpool.org"});
        mode.setAdapter(a);mode.setSelection(cfg.solo?1:0);base.addView(mode);
        pool=edit(base,"Pool endpoint (host:port)",cfg.pool,"example.pool.net:3333");
        wallet=edit(base,"Payout wallet / pool account",cfg.wallet,"Your address or pool username");
        password=edit(base,"Stratum password (usually x)",cfg.password,"x");
        suggested=edit(base,"Suggested difficulty (optional)",cfg.suggested,"0.0001");
        threads=edit(base,"CPU threads (1–"+Runtime.getRuntime().availableProcessors()+")",String.valueOf(cfg.threads),"1");
        threads.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        save=button("Save configuration");base.addView(save);save.setOnClickListener(v->saveConfig(true));
        info=text("LAN discovery: HTTP :8080 and mDNS _http._tcp while mining. The existing CRS Mining monitor needs its optional v1.3 compatibility update.",12,0xff9eaebd,false);
        base.addView(info);
        base.addView(text("Home-screen widget: long-press your home screen → Widgets → CRSminer for Android. Shows live rate and shares, with Start/Stop controls.",12,0xff9eaebd,false));
        base.addView(text("While mining, a foreground service and partial CPU wake lock help keep the miner active with the screen off. Android battery limits and thermal safety still apply.",12,0xffd7bb78,false));
        base.addView(text("CPU mining can cause heat and battery wear. Severe system thermal status pauses hashing. Solo mode uses a solo pool, not your own Bitcoin Core node.",12,0xffd7bb78,false));
    }
    private int dip(int n){return (int)(getResources().getDisplayMetrics().density*n+.5f);}
    private boolean saveConfig(boolean show){
        try{
            Config c=new Config();c.name=name.getText().toString().trim().toLowerCase(Locale.ROOT);
            c.worker=worker.getText().toString().trim();c.pool=pool.getText().toString().trim();
            c.wallet=wallet.getText().toString().trim();c.password=password.getText().toString();
            c.suggested=suggested.getText().toString().trim();c.solo=(mode.getSelectedItemPosition()==1);
            c.threads=Integer.parseInt(threads.getText().toString().trim());c.validate();c.save(this);cfg=c;
            if(show)Toast.makeText(this,"Saved. Restart mining to apply settings.",Toast.LENGTH_LONG).show();return true;
        }catch(Exception ex){Toast.makeText(this,"Configuration: "+ex.getMessage(),Toast.LENGTH_LONG).show();return false;}
    }
    private void start(){
        if(!saveConfig(false))return;
        Intent i=new Intent(this,MiningService.class).setAction(MiningService.ACTION_START);
        if(Build.VERSION.SDK_INT>=26)startForegroundService(i);else startService(i);
        Toast.makeText(this,"Starting CRSminer",Toast.LENGTH_SHORT).show();
    }
    private void stop(){startService(new Intent(this,MiningService.class).setAction(MiningService.ACTION_STOP));}
    private void refresh(){
        JSONObject s=MiningService.snapshot(this);String stage=s.optString("stage","Stopped");
        status.setText("● " + stage);
        hashrate.setText(hashFmt(s.optDouble("hr",0)));
        shares.setText("Accepted "+s.optLong("acc")+"   •   Rejected "+s.optLong("rej"));
        difficulty.setText(String.format(Locale.US,"Pool diff %.6g  •  Best %.6g",s.optDouble("pdiff",0),s.optDouble("best",0)));
        String ip=s.optString("ip","");
        details.setText("Jobs "+s.optLong("jobs")+"   •   Uptime "+s.optLong("up")+"s   •   Pool " +s.optString("pool", "—")+"\n"
                +(ip.isEmpty()?"":("LAN: http://"+ip+":"+MiningService.HTTP_PORT+"/api/status\n"))
                +(s.optString("diag","").isEmpty()?"":("Attention: "+s.optString("diag","")+"\n"))
                +(s.optString("err","").isEmpty()?"":("Last error: "+s.optString("err",""))));
        boolean mining=!stage.equalsIgnoreCase("stopped") && !stage.equalsIgnoreCase("Configuration/self-test error");
        start.setEnabled(!mining);stop.setEnabled(mining);
    }
    private String hashFmt(double h){
        String[] u={"H/s","kH/s","MH/s","GH/s"};int i=0;while(h>=1000&&i<3){h/=1000;i++;}
        return String.format(Locale.US,"%.2f %s",h,u[i]);
    }
    @Override protected void onDestroy(){open=false;handler.removeCallbacks(ticker);super.onDestroy();}
}
