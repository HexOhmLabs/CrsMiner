package com.hexohm.crsminerandroid;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.SizeF;
import android.util.TypedValue;
import android.widget.RemoteViews;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Resizable HexOhm home-screen widget: layout, font sizes and button dimensions
 * follow each individual widget's bounds (not a shared global size).
 * Start/Stop remain explicit user actions; resizing never starts mining.
 */
public final class MinerWidget extends AppWidgetProvider {
    private static final String START="com.hexohm.crsminerandroid.WIDGET_START";
    private static final String STOP="com.hexohm.crsminerandroid.WIDGET_STOP";

    @Override public void onReceive(Context context,Intent intent){
        super.onReceive(context,intent);
        if(intent==null)return;
        String action=intent.getAction();
        if(START.equals(action)||STOP.equals(action)){
            if((START.equals(action) && MiningService.isRunning()) ||
                    (STOP.equals(action) && !MiningService.isRunning())){
                updateAll(context);return;
            }
            Intent mine=new Intent(context,MiningService.class).setAction(
                    START.equals(action)?MiningService.ACTION_START:MiningService.ACTION_STOP);
            try{
                // Home-screen widget presses are direct user interactions.
                if(START.equals(action) && Build.VERSION.SDK_INT>=26)
                    context.startForegroundService(mine);
                else context.startService(mine);
            }catch(RuntimeException e){
                Intent open=new Intent(context,MainActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try{context.startActivity(open);}catch(RuntimeException ignored){}
            }
            updateAll(context);
        }
    }

    @Override public void onUpdate(Context context, AppWidgetManager manager, int[] ids){
        for(int id:ids) updateWidget(context,manager,id);
    }

    /** Called by the launcher whenever the user stretches or shrinks the widget. */
    @Override public void onAppWidgetOptionsChanged(Context context, AppWidgetManager manager,
                                                     int appWidgetId, Bundle newOptions){
        super.onAppWidgetOptionsChanged(context,manager,appWidgetId,newOptions);
        updateWidget(context,manager,appWidgetId);
    }

    static void updateAll(Context context){
        AppWidgetManager mgr=AppWidgetManager.getInstance(context);
        ComponentName provider=new ComponentName(context,MinerWidget.class);
        for(int id:mgr.getAppWidgetIds(provider)) updateWidget(context,mgr,id);
    }

    private static void updateWidget(Context ctx,AppWidgetManager mgr,int id){
        Bundle opt=mgr.getAppWidgetOptions(id);
        // On Android 12+ provide a view for EVERY size reported by the launcher.
        // Handles portrait/landscape and folding without forcing a single layout.
        if(Build.VERSION.SDK_INT>=31 && opt!=null){
            @SuppressWarnings("deprecation")
            ArrayList<SizeF> sizes=opt.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES);
            if(sizes!=null && !sizes.isEmpty()){
                Map<SizeF,RemoteViews> responsive=new LinkedHashMap<>();
                for(SizeF size:sizes){
                    if(size!=null && size.getWidth()>0 && size.getHeight()>0)
                        responsive.put(size,views(ctx,size.getWidth(),size.getHeight()));
                }
                if(!responsive.isEmpty()){
                    mgr.updateAppWidget(id,new RemoteViews(responsive));
                    return;
                }
            }
        }
        float w=250f,h=140f;
        if(opt!=null){
            w=opt.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH,250);
            h=opt.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT,140);
            if(w<=0)w=250;
            if(h<=0)h=140;
        }
        mgr.updateAppWidget(id,views(ctx,w,h));
    }

    private static RemoteViews views(Context context,float widthDp,float heightDp){
        final boolean compact = widthDp<235f || heightDp<126f;
        final boolean expanded = !compact && widthDp>=315f && heightDp>=205f;
        final int layout=compact?R.layout.miner_widget_compact:
                expanded?R.layout.miner_widget_expanded:R.layout.miner_widget;
        // Clamp font growth: readable at a small size without clipping at extremes.
        final float baseW=compact?215f:(expanded?380f:285f);
        final float baseH=compact?110f:(expanded?250f:160f);
        float scale=clamp(Math.min(widthDp/baseW,heightDp/baseH),
                compact?0.82f:0.86f,expanded?1.40f:1.24f);
        RemoteViews v=new RemoteViews(context.getPackageName(),layout);
        JSONObject s=MiningService.snapshot(context);
        String stage=s.optString("stage","Stopped");
        boolean active=!(stage.equalsIgnoreCase("stopped")||
                stage.equalsIgnoreCase("Configuration/self-test error"));
        v.setTextViewText(R.id.widget_stage,stage);
        v.setTextViewText(R.id.widget_rate,hashrate(s.optDouble("hr",0)));
        v.setTextViewText(R.id.widget_shares,"Accepted "+s.optLong("acc")+
                "  •  Rejected "+s.optLong("rej"));
        v.setTextViewText(R.id.widget_start,active?"Running":"Start");
        v.setTextColor(R.id.widget_start,active?0xff606876:0xff10151d);
        v.setTextViewText(R.id.widget_stop,"Stop");
        v.setTextColor(R.id.widget_stop,active?0xff10151d:0xff606876);

        sizeSp(v,R.id.widget_title,(compact?11f:(expanded?17f:13f))*scale);
        sizeSp(v,R.id.widget_stage,(compact?10f:(expanded?14f:11f))*scale);
        sizeSp(v,R.id.widget_rate,(compact?20f:(expanded?36f:25f))*scale);
        sizeSp(v,R.id.widget_shares,(compact?10f:(expanded?14f:11f))*scale);
        sizeSp(v,R.id.widget_start,(compact?11f:(expanded?16f:13f))*scale);
        sizeSp(v,R.id.widget_stop,(compact?11f:(expanded?16f:13f))*scale);
        // Height changes too (on Android 12+); earlier Android uses the 3 XML
        // variants' different 29dp / 34dp / 48dp button heights.
        if(Build.VERSION.SDK_INT>=31){
            float buttonDp=(compact?29f:(expanded?48f:34f))*scale;
            v.setViewLayoutHeight(R.id.widget_start,buttonDp,TypedValue.COMPLEX_UNIT_DIP);
            v.setViewLayoutHeight(R.id.widget_stop,buttonDp,TypedValue.COMPLEX_UNIT_DIP);
        }
        // Keep margins/padding proportional in each breakpoint. The watermark
        // stays behind all content at a fixed 7.5% opacity (XML).
        int p=Math.round((compact?7f:(expanded?16f:11f))*scale*
                context.getResources().getDisplayMetrics().density);
        v.setViewPadding(R.id.widget_content,p,p,p,p);

        Intent open=new Intent(context,MainActivity.class);
        PendingIntent openPending=PendingIntent.getActivity(context,600,open,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        v.setOnClickPendingIntent(R.id.widget_title,openPending);
        v.setOnClickPendingIntent(R.id.widget_rate,openPending);
        v.setOnClickPendingIntent(R.id.widget_stage,openPending);

        Intent start=new Intent(context,MinerWidget.class).setAction(START);
        Intent stop=new Intent(context,MinerWidget.class).setAction(STOP);
        v.setOnClickPendingIntent(R.id.widget_start,PendingIntent.getBroadcast(context,601,start,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT));
        v.setOnClickPendingIntent(R.id.widget_stop,PendingIntent.getBroadcast(context,602,stop,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT));
        return v;
    }
    private static void sizeSp(RemoteViews v,int id,float points){
        v.setTextViewTextSize(id,TypedValue.COMPLEX_UNIT_SP,points);
    }
    private static float clamp(float x,float lo,float hi){return Math.max(lo,Math.min(hi,x));}
    private static String hashrate(double rate){
        if(!Double.isFinite(rate)||rate<0)rate=0;
        String[] units={"H/s","kH/s","MH/s","GH/s"};int i=0;
        while(rate>=1000&&i<units.length-1){rate/=1000;i++;}
        return String.format(Locale.US,"%.2f %s",rate,units[i]);
    }
}
