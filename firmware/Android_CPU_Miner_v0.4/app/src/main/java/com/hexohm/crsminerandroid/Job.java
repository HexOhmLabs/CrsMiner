package com.hexohm.crsminerandroid;

import org.json.JSONArray;
import org.json.JSONException;
import java.util.Arrays;

/** Immutable Stratum V1 job; header construction exactly follows the ESP CRSminer word order. */
final class Job {
    final String id, version, nbits, ntime;
    final byte[] prev, coinb1, coinb2, extranonce1;
    final byte[][] branches;
    final int en2bytes;
    final long generation;
    Job(JSONArray p, byte[] en1, int en2bytes, long gen) throws JSONException {
        id=p.getString(0); prev=Hex.reverse4(Hex.decode(p.getString(1)));
        coinb1=Hex.decode(p.getString(2)); coinb2=Hex.decode(p.getString(3));
        JSONArray arr=p.getJSONArray(4);branches=new byte[arr.length()][];
        for(int i=0;i<arr.length();i++) {branches[i]=Hex.decode(arr.getString(i));if(branches[i].length!=32)throw new IllegalArgumentException("Bad branch");}
        version=p.getString(5); nbits=p.getString(6); ntime=p.getString(7);
        if(prev.length!=32||en1.length==0||en2bytes<1||en2bytes>8)throw new IllegalArgumentException("Invalid job");
        extranonce1=en1.clone();this.en2bytes=en2bytes;generation=gen;
    }
    byte[] header76(long extranonce2) {
        byte[] cb=Hex.concat(coinb1,extranonce1,Hex.decode(Hex.leHex(extranonce2,en2bytes)),coinb2);
        byte[] merkle=Hex.sha2(cb);
        for(byte[] branch:branches)merkle=Hex.sha2(Hex.concat(merkle,branch));
        byte[] header=Hex.concat(Hex.le32(version),prev,merkle,Hex.le32(ntime),Hex.le32(nbits));
        if(header.length!=76)throw new IllegalStateException("Header length");return header;
    }
    String extra2Hex(long extranonce2) {
        // Stratum extranonce2 is sent in the same byte order used in coinbase.
        return Hex.leHex(extranonce2,en2bytes);
    }
}
