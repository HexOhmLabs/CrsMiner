package com.hexohm.crsminerandroid;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.RoundingMode;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;

final class Hex {
    private static final char[] CH="0123456789abcdef".toCharArray();
    // SHA-256 pool difficulty 1 target (the Bitcoin "diff1" target).
    private static final BigInteger DIFF1=new BigInteger("00000000ffff0000000000000000000000000000000000000000000000000000",16);
    private static final BigInteger MAX=BigInteger.ONE.shiftLeft(256).subtract(BigInteger.ONE);
    static byte[] decode(String s) {
        if(s==null || (s.length()&1)!=0) throw new IllegalArgumentException("Invalid hex length");
        byte[] r=new byte[s.length()/2];
        for(int i=0;i<r.length;i++) {
            int a=Character.digit(s.charAt(i*2),16),b=Character.digit(s.charAt(i*2+1),16);
            if(a<0||b<0) throw new IllegalArgumentException("Invalid hex");
            r[i]=(byte)(a*16+b);
        }
        return r;
    }
    static String encode(byte[] b) {
        char[] out=new char[b.length*2];
        for(int i=0;i<b.length;i++){out[2*i]=CH[(b[i]>>>4)&15];out[2*i+1]=CH[b[i]&15];}
        return new String(out);
    }
    static byte[] concat(byte[]... all) {
        int n=0; for(byte[] a:all)n+=a.length;
        byte[] v=new byte[n];int pos=0;
        for(byte[] a:all){System.arraycopy(a,0,v,pos,a.length);pos+=a.length;}
        return v;
    }
    static byte[] reversed(byte[] a) {byte[] b=a.clone();for(int i=0;i<b.length/2;i++){byte t=b[i];b[i]=b[b.length-1-i];b[b.length-1-i]=t;}return b;}
    static byte[] reverse4(byte[] a) {
        if((a.length&3)!=0)throw new IllegalArgumentException("Word alignment");
        byte[] b=a.clone(); for(int i=0;i<a.length;i+=4)for(int j=0;j<4;j++)b[i+j]=a[i+3-j];return b;
    }
    static byte[] le32(String bigEndianHex) { byte[] b=decode(bigEndianHex); if(b.length!=4)throw new IllegalArgumentException("Not uint32");return reversed(b); }
    static byte[] sha2(byte[] v){try{MessageDigest d=MessageDigest.getInstance("SHA-256");return d.digest(d.digest(v));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
    static byte[] target(double diff) {
        if(!Double.isFinite(diff)||diff<=0)throw new IllegalArgumentException("Invalid difficulty");
        BigInteger target=new BigDecimal(DIFF1).divide(BigDecimal.valueOf(diff),0,RoundingMode.DOWN).toBigInteger().min(MAX);
        byte[] raw=target.toByteArray(),out=new byte[32]; int len=Math.min(32,raw.length);
        System.arraycopy(raw,raw.length-len,out,32-len,len);return out;
    }
    static double difficulty(byte[] digest) {
        BigInteger val=new BigInteger(1,reversed(digest));
        if(val.signum()==0)return Double.POSITIVE_INFINITY;
        return new BigDecimal(DIFF1).divide(new BigDecimal(val),8,RoundingMode.HALF_UP).doubleValue();
    }
    static String leHex(long v, int nbytes) {
        byte[] bytes=new byte[nbytes];for(int i=0;i<nbytes;i++)bytes[i]=(byte)(v>>>(8*i));return encode(bytes);
    }
    static String netHex(long v) {return String.format(java.util.Locale.US,"%08x",v&0xffffffffL);}
    static boolean littleHashUnderTarget(byte[] digest, byte[] targetBE){
        for(int i=0;i<32;i++){int a=digest[31-i]&255,b=targetBE[i]&255;if(a<b)return true;if(a>b)return false;}return true;
    }
}
