// CRSminer for Android / HexOhm. Portable SHA-256d CPU engine (no external crypto library).
#include <cstdint>
#include <cstring>
#ifdef CRS_DESKTOP_TEST
#include <cstdio>
#else
#include <jni.h>
#endif
namespace {
constexpr uint32_t K[64]={
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
constexpr uint32_t IV[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
inline uint32_t ror(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
struct S {uint32_t h[8];};
void init(S& s){std::memcpy(s.h,IV,sizeof(IV));}
void block(S& s,const uint8_t* p){
 uint32_t w[64];for(int i=0;i<16;i++)w[i]=((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
 for(int i=16;i<64;i++){
  uint32_t x=w[i-15],y=w[i-2];
  w[i]=(ror(y,17)^ror(y,19)^(y>>10))+w[i-7]+(ror(x,7)^ror(x,18)^(x>>3))+w[i-16];
 }
 uint32_t a=s.h[0],b=s.h[1],c=s.h[2],d=s.h[3],e=s.h[4],f=s.h[5],g=s.h[6],h=s.h[7];
 for(int i=0;i<64;i++){
  uint32_t t1=h+(ror(e,6)^ror(e,11)^ror(e,25))+((e&f)^(~e&g))+K[i]+w[i];
  uint32_t t2=(ror(a,2)^ror(a,13)^ror(a,22))+((a&b)^(a&c)^(b&c));
  h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
 }
 s.h[0]+=a;s.h[1]+=b;s.h[2]+=c;s.h[3]+=d;s.h[4]+=e;s.h[5]+=f;s.h[6]+=g;s.h[7]+=h;
}
void digest(const S& s,uint8_t* out){for(int i=0;i<8;i++){
 out[4*i]=(uint8_t)(s.h[i]>>24);out[4*i+1]=(uint8_t)(s.h[i]>>16);
 out[4*i+2]=(uint8_t)(s.h[i]>>8);out[4*i+3]=(uint8_t)s.h[i];
}}
void sha(const uint8_t* input,size_t length,uint8_t* out){
 S s;init(s);size_t pos=0;
 while(length-pos>=64){block(s,input+pos);pos+=64;}
 uint8_t pad[128]={0};size_t rem=length-pos;
 if(rem)std::memcpy(pad,input+pos,rem);
 pad[rem]=0x80;size_t total=rem<56?64:128;
 uint64_t bits=(uint64_t)length*8;
 for(int j=0;j<8;j++)pad[total-1-j]=(uint8_t)(bits>>(8*j));
 block(s,pad);if(total==128)block(s,pad+64);digest(s,out);
}
void sha2(const uint8_t* input,size_t length,uint8_t* out){uint8_t once[32];sha(input,length,once);sha(once,32,out);}
bool under(const uint8_t hash[32],const uint8_t target[32]){
 for(int i=0;i<32;i++){
  uint8_t a=hash[31-i],b=target[i];
  if(a<b)return true;if(a>b)return false;
 }
 return true;
}
// Reuse the midstate of the first 64 header bytes while scanning the nonce.
// Equivalent to the ESP software hashing approach, but portable ARM/x86 C++.
void sha2header(const S& mid,const uint8_t tail[64],uint8_t out[32]) {
 S first=mid;block(first,tail);
 uint8_t once[32];digest(first,once);
 sha(once,32,out);
}
bool selftest(){
 uint8_t out[32];sha((const uint8_t*)"abc",3,out);
 if(out[0]!=0xba||out[1]!=0x78||out[31]!=0xad)return false;
 // Bitcoin genesis header, nonce=0x7c2bac1d (2083236893), in on-wire little endian.
 constexpr uint8_t genesis[80]={
  0x01,0x00,0x00,0x00,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0x3b,0xa3,0xed,0xfd,0x7a,0x7b,0x12,0xb2,0x7a,0xc7,0x2c,0x3e,0x67,0x76,0x8f,0x61,
  0x7f,0xc8,0x1b,0xc3,0x88,0x8a,0x51,0x32,0x3a,0x9f,0xb8,0xaa,0x4b,0x1e,0x5e,0x4a,
  0x29,0xab,0x5f,0x49,0xff,0xff,0x00,0x1d,0x1d,0xac,0x2b,0x7c};
 sha2(genesis,80,out);
 if(!(out[31]==0x00&&out[30]==0x00&&out[29]==0x00&&out[28]==0x00&&out[27]==0x00
     &&out[26]==0x19&&out[25]==0xd6&&out[24]==0x68))return false;
 // Cross-check the optimized midstate SHA-256d on the genesis nonce AND non-genesis nonce.
 S mid;init(mid);block(mid,genesis);
 uint8_t tail[64]={0};std::memcpy(tail,genesis+64,16);
 tail[16]=0x80;tail[62]=0x02;tail[63]=0x80; // 80-byte message = 640 bits
 uint8_t fast[32];sha2header(mid,tail,fast);
 if(std::memcmp(fast,out,32)!=0)return false;
 tail[12]^=1;sha2header(mid,tail,fast);
 uint8_t changed[80];std::memcpy(changed,genesis,80);changed[76]^=1;
 sha2(changed,80,out);
 return std::memcmp(fast,out,32)==0;
}
}
#ifdef CRS_DESKTOP_TEST
int main(){std::printf("SHA256 and Bitcoin genesis selftest: %s\n",selftest()?"PASS":"FAIL");return selftest()?0:1;}
#else
extern "C" JNIEXPORT jboolean JNICALL Java_com_hexohm_crsminerandroid_NativeHash_selfTest(JNIEnv*,jclass){return selftest()?JNI_TRUE:JNI_FALSE;}
extern "C" JNIEXPORT jlong JNICALL Java_com_hexohm_crsminerandroid_NativeHash_scan(JNIEnv* env,jclass,jbyteArray prefix,jbyteArray target,jlong start,jint count){
 if(!prefix||!target||env->GetArrayLength(prefix)!=76||env->GetArrayLength(target)!=32||count<1||count>1000000)return -1;
 uint8_t head[76],th[32],hash[32];env->GetByteArrayRegion(prefix,0,76,(jbyte*)head);env->GetByteArrayRegion(target,0,32,(jbyte*)th);
 S mid;init(mid);block(mid,head); // first 64 bytes are unchanged for all nonces in the batch
 uint8_t tail[64]={0};std::memcpy(tail,head+64,12);
 tail[16]=0x80;tail[62]=0x02;tail[63]=0x80;
 for(jint i=0;i<count;i++){
  uint32_t n=(uint32_t)((uint64_t)start+(uint32_t)i);
  tail[12]=(uint8_t)n;tail[13]=(uint8_t)(n>>8);tail[14]=(uint8_t)(n>>16);tail[15]=(uint8_t)(n>>24);
  sha2header(mid,tail,hash);
  // ESP-like progress: report 12-leading-zero-bit near-misses, as well as real shares.
  // Java always verifies the candidate; only hashes under the full pool target are submitted.
  if(under(hash,th) || (hash[31]==0 && (hash[30]&0xf0)==0))return (jlong)n;
 }
 return -1;
}
#endif
