package com.hexohm.crsminerandroid;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.Locale;
import java.util.regex.Pattern;

final class Config {
    static final String PREFS="crsminer";
    private static final Pattern ID=Pattern.compile("[A-Za-z0-9]{4}");
    private static final Pattern WORKER=Pattern.compile("[A-Za-z0-9_-]{1,32}");
    String name="a001", pool="", wallet="", worker="crs", password="x", suggested="";
    int threads=1;
    boolean solo=false;
    static Config load(Context ctx) {
        SharedPreferences p=ctx.getSharedPreferences(PREFS,0);
        Config c=new Config();
        c.name=p.getString("name",c.name); c.pool=p.getString("pool","");
        c.wallet=p.getString("wallet",""); c.worker=p.getString("worker","crs");
        c.password=p.getString("password","x"); c.suggested=p.getString("sugg","");
        c.threads=p.getInt("threads",1); c.solo=p.getBoolean("solo",false); return c;
    }
    void save(Context ctx) {
        ctx.getSharedPreferences(PREFS,0).edit()
                .putString("name",name).putString("pool",pool).putString("wallet",wallet)
                .putString("worker",worker).putString("password",password)
                .putString("sugg",suggested).putInt("threads",threads).putBoolean("solo",solo).apply();
    }
    String hostname() { return "crsminer-"+name.toLowerCase(Locale.ROOT); }
    String endpoint() { return solo?"stratum.ckpool.org:3333":pool; }
    void validate() {
        if(!ID.matcher(name).matches()) throw new IllegalArgumentException("Miner ID must be exactly 4 letters/digits");
        if(!WORKER.matcher(worker).matches()) throw new IllegalArgumentException("Worker: letters/digits/_/- only");
        if(wallet.isEmpty() || wallet.contains(" ") || wallet.contains("\n")) throw new IllegalArgumentException("Enter your payout address / pool username");
        if(!endpoint().matches("[A-Za-z0-9.:-]+:[0-9]{2,5}")) throw new IllegalArgumentException("Pool must be host:port");
        int port=Integer.parseInt(endpoint().substring(endpoint().lastIndexOf(':')+1));
        if(port<1 || port>65535) throw new IllegalArgumentException("Invalid pool port");
        if(threads<1 || threads>Runtime.getRuntime().availableProcessors()) throw new IllegalArgumentException("Invalid CPU threads");
        if(!suggested.isEmpty()) {
            double d;
            try { d=Double.parseDouble(suggested); } catch(NumberFormatException e) { throw new IllegalArgumentException("Invalid difficulty"); }
            if(!(d>0 && Double.isFinite(d))) throw new IllegalArgumentException("Difficulty must be positive");
        }
    }
}
