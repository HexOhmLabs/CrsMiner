/* HexOhm CRSminer PC - SHA-256d native nonce scanner, v0.2.0.
 *
 * Standalone C11 source, no CRT dependency, no network functions, no file access.
 * Hashes exactly the same 80-byte header as the Python SHA-256d implementation.
 * The 64-byte SHA midstate is computed once per scan call and shared by all nonces.
 *
 * Engines (picked at run time from CPUID; every engine returns identical results):
 *   0  scalar  - portable C, any x86-64 (or other) CPU
 *   1  sha-ni  - Intel SHA extensions, 2 hashes interleaved
 *   2  avx2    - 8 hashes in parallel (one per 32-bit lane)
 * Vector engines only compute the cheap top 32 bits of each digest. A nonce whose
 * top word cannot beat max(target, best-so-far) is discarded; the rare survivors are
 * re-hashed with the scalar engine for exact comparison. No share can be missed.
 *
 * MIT License - see project LICENSE.
 */
#include <stdint.h>
#include <stddef.h>
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
# define CRS_X86 1
# include <immintrin.h>
#endif

#if defined(_WIN32)
# define CRS_EXPORT __declspec(dllexport)
#else
# define CRS_EXPORT __attribute__((visibility("default")))
#endif

enum { ENG_SCALAR = 0, ENG_SHANI = 1, ENG_AVX2 = 2 };

static const uint32_t K[64] __attribute__((aligned(16))) = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static const uint32_t IV[8] __attribute__((aligned(16))) = {
  0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
  0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
};

static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32-n)); }
static inline uint32_t bswap32(uint32_t x) {
  return (x>>24)|((x>>8)&0xff00u)|((x<<8)&0xff0000u)|(x<<24);
}
static inline uint32_t rd_be32(const uint8_t *b) {
  return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
}
static inline void wr_be32(uint8_t *b, uint32_t v) {
  b[0]=(uint8_t)(v>>24); b[1]=(uint8_t)(v>>16); b[2]=(uint8_t)(v>>8); b[3]=(uint8_t)v;
}

/* ---------------------------------------------------------------- scalar core */
static void compress_w(uint32_t state[8], const uint32_t in[16]) {
  uint32_t w[64], i;
  for (i=0;i<16;i++) w[i]=in[i];
  for (i=16;i<64;i++) {
    uint32_t x=w[i-15],y=w[i-2];
    w[i]=(rotr(y,17)^rotr(y,19)^(y>>10))+w[i-7]+(rotr(x,7)^rotr(x,18)^(x>>3))+w[i-16];
  }
  uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
  for (i=0;i<64;i++) {
    uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25);
    uint32_t t1=h+s1+(g^(e&(f^g)))+K[i]+w[i];
    uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
    uint32_t t2=s0+((a&b)|(c&(a|b)));
    h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
  state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
static void midstate(const uint8_t prefix76[76], uint32_t s[8]) {
  uint32_t w[16], i;
  for (i=0;i<8;i++) s[i]=IV[i];
  for (i=0;i<16;i++) w[i]=rd_be32(prefix76+4*i);
  compress_w(s,w);
}
/* tail[0..2] = big-endian words of header bytes 64..75 */
static void hash_scalar(const uint32_t mid[8], const uint32_t tail[3], uint32_t nonce, uint32_t out_state[8]) {
  uint32_t w[16], s[8], i;
  for (i=0;i<8;i++) s[i]=mid[i];
  w[0]=tail[0]; w[1]=tail[1]; w[2]=tail[2]; w[3]=bswap32(nonce);
  w[4]=0x80000000u; for(i=5;i<15;i++) w[i]=0; w[15]=640;
  compress_w(s,w);
  for (i=0;i<8;i++) w[i]=s[i];
  w[8]=0x80000000u; for(i=9;i<15;i++) w[i]=0; w[15]=256;
  for (i=0;i<8;i++) s[i]=IV[i];
  compress_w(s,w);
  for (i=0;i<8;i++) out_state[i]=s[i];
}
static void state_to_digest(const uint32_t st[8], uint8_t d[32]) {
  for (int i=0;i<8;i++) wr_be32(d+4*i, st[i]);
}
/* Compare digests as little-endian 256-bit numbers. */
static int digest_lt(const uint8_t *a,const uint8_t *b) {
  for(int i=31;i>=0;i--) { if(a[i]<b[i])return 1;if(a[i]>b[i])return 0; }return 0;
}
static int digest_le(const uint8_t *a,const uint8_t *b) {
  for(int i=31;i>=0;i--) { if(a[i]<b[i])return 1;if(a[i]>b[i])return 0; }return 1;
}

/* Shared scan context. */
typedef struct {
  uint32_t mid[8], tail[3];
  const uint8_t *target;          /* 32 bytes LE */
  uint8_t best[32];
  uint32_t *hits; uint32_t max_hits, found;
  uint32_t thr;                   /* top-32-bit prefilter threshold */
  uint32_t target_top;
} scan_ctx;

static inline uint32_t digest_top32(const uint8_t *d) {   /* digest bytes 28..31 read LE */
  return (uint32_t)d[28]|((uint32_t)d[29]<<8)|((uint32_t)d[30]<<16)|((uint32_t)d[31]<<24);
}
static void refresh_thr(scan_ctx *c) {
  uint32_t bt = digest_top32(c->best);
  c->thr = bt > c->target_top ? bt : c->target_top;
}
/* Exact evaluation of one nonce; returns 1 if the hit buffer is now full. */
static int exact_check(scan_ctx *c, uint32_t nonce) {
  uint32_t st[8]; uint8_t dg[32];
  hash_scalar(c->mid, c->tail, nonce, st);
  state_to_digest(st, dg);
  if (digest_lt(dg, c->best)) { for(int j=0;j<32;j++) c->best[j]=dg[j]; refresh_thr(c); }
  if (digest_le(dg, c->target)) { c->hits[c->found++] = nonce; if (c->found >= c->max_hits) return 1; }
  return 0;
}

static uint32_t scan_scalar(scan_ctx *c, uint32_t start, uint32_t count) {
  uint32_t i, st[8];
  for (i=0;i<count;i++) {
    uint32_t nonce = start+i;
    hash_scalar(c->mid, c->tail, nonce, st);
    if (bswap32(st[7]) <= c->thr) {     /* cheap reject for ~all nonces */
      if (exact_check(c, nonce)) return i+1;
    }
  }
  return count;
}

#ifdef CRS_X86
/* ------------------------------------------------------------------- SHA-NI */
#define NI_TGT __attribute__((target("sha,sse4.1,ssse3")))
#define NI_LANES 2

/* One 64-byte block for NI_LANES independent hashes. W[l][0..3] are the 16 message words as 4 vectors.
 * st0/st1 are in the ABEF/CDGH layout expected by sha256rnds2. */
NI_TGT static inline void ni_rounds(__m128i st0[NI_LANES], __m128i st1[NI_LANES], __m128i W[NI_LANES][16/4]) {
  __m128i m[NI_LANES][4+16];  /* 20 vectors incl. schedule */
  (void)m;
  /* schedule: vectors 0..3 given, 4..15 computed (each vector = 4 words) */
  __m128i V[NI_LANES][16];
  for (int l=0;l<NI_LANES;l++) {
    for (int i=0;i<4;i++) V[l][i]=W[l][i];
    for (int i=4;i<16;i++) {
      __m128i t = _mm_sha256msg1_epu32(V[l][i-4], V[l][i-3]);
      t = _mm_add_epi32(t, _mm_alignr_epi8(V[l][i-1], V[l][i-2], 4));
      V[l][i] = _mm_sha256msg2_epu32(t, V[l][i-1]);
    }
  }
  for (int i=0;i<16;i++) {
    __m128i k = _mm_load_si128((const __m128i*)(K+4*i));
    for (int l=0;l<NI_LANES;l++) {
      __m128i msg = _mm_add_epi32(V[l][i], k);
      st1[l] = _mm_sha256rnds2_epu32(st1[l], st0[l], msg);
      msg = _mm_shuffle_epi32(msg, 0x0E);
      st0[l] = _mm_sha256rnds2_epu32(st0[l], st1[l], msg);
    }
  }
}
/* mid state (DCBA/HGFE order) -> ABEF/CDGH */
NI_TGT static inline void ni_pack(const uint32_t s[8], __m128i *st0, __m128i *st1) {
  __m128i abcd = _mm_loadu_si128((const __m128i*)s);
  __m128i efgh = _mm_loadu_si128((const __m128i*)(s+4));
  __m128i tmp = _mm_shuffle_epi32(abcd, 0xB1);
  efgh = _mm_shuffle_epi32(efgh, 0x1B);
  *st0 = _mm_alignr_epi8(tmp, efgh, 8);
  *st1 = _mm_blend_epi16(efgh, tmp, 0xF0);
}
NI_TGT static inline void ni_unpack(__m128i st0, __m128i st1, __m128i *abcd, __m128i *efgh) {
  __m128i tmp = _mm_shuffle_epi32(st0, 0x1B);
  st1 = _mm_shuffle_epi32(st1, 0xB1);
  *abcd = _mm_blend_epi16(tmp, st1, 0xF0);
  *efgh = _mm_alignr_epi8(st1, tmp, 8);
}

NI_TGT static uint32_t scan_shani(scan_ctx *c, uint32_t start, uint32_t count) {
  uint32_t i = 0;
  __m128i mid0, mid1, iv0, iv1;
  ni_pack(c->mid, &mid0, &mid1);
  ni_pack(IV, &iv0, &iv1);
  const __m128i fixed_hi = _mm_set_epi32(640, 0, 0, (int)0x80000000u); /* words 4..7 of block 1 (word4 = pad) */
  (void)fixed_hi;
  const __m128i z = _mm_setzero_si128();
  const __m128i first_w0 = _mm_set_epi32(0, (int)c->tail[2], (int)c->tail[1], (int)c->tail[0]); /* lane order: w0,w1,w2,w3 */
  for (; i + NI_LANES <= count; i += NI_LANES) {
    __m128i st0[NI_LANES], st1[NI_LANES], W[NI_LANES][4];
    uint32_t n[NI_LANES];
    for (int l=0;l<NI_LANES;l++) {
      n[l] = start + i + (uint32_t)l;
      st0[l]=mid0; st1[l]=mid1;
      W[l][0] = _mm_insert_epi32(first_w0, (int)bswap32(n[l]), 3);
      W[l][1] = _mm_set_epi32(0, 0, 0, (int)0x80000000u);
      W[l][2] = z;
      W[l][3] = _mm_set_epi32(640, 0, 0, 0);
    }
    ni_rounds(st0, st1, W);
    __m128i s0[NI_LANES], s1[NI_LANES], W2[NI_LANES][4];
    for (int l=0;l<NI_LANES;l++) {
      /* add midstate (feed-forward) */
      st0[l] = _mm_add_epi32(st0[l], mid0);
      st1[l] = _mm_add_epi32(st1[l], mid1);
      ni_unpack(st0[l], st1[l], &W2[l][0], &W2[l][1]);
      W2[l][2] = _mm_set_epi32(0, 0, 0, (int)0x80000000u);
      W2[l][3] = _mm_set_epi32(256, 0, 0, 0);
      s0[l]=iv0; s1[l]=iv1;
    }
    ni_rounds(s0, s1, W2);
    for (int l=0;l<NI_LANES;l++) {
      s0[l] = _mm_add_epi32(s0[l], iv0);
      s1[l] = _mm_add_epi32(s1[l], iv1);
      __m128i abcd, efgh;
      ni_unpack(s0[l], s1[l], &abcd, &efgh);
      uint32_t h7 = (uint32_t)_mm_extract_epi32(efgh, 3);
      if (bswap32(h7) <= c->thr) {
        if (exact_check(c, n[l])) return i + (uint32_t)l + 1;
      }
    }
  }
  /* scalar tail */
  uint32_t used = scan_scalar(c, start + i, count - i);
  return i + used;
}

/* --------------------------------------------------------------------- AVX2 */
#define AVX_TGT __attribute__((target("avx2")))
#define V8 __m256i
#define ADD(a,b) _mm256_add_epi32(a,b)
#define XOR(a,b) _mm256_xor_si256(a,b)
#define AND(a,b) _mm256_and_si256(a,b)
#define OR(a,b)  _mm256_or_si256(a,b)
#define ROR(x,n) OR(_mm256_srli_epi32(x,n), _mm256_slli_epi32(x,32-(n)))
#define SHR(x,n) _mm256_srli_epi32(x,n)
#define S0(x) XOR(XOR(ROR(x,7),ROR(x,18)),SHR(x,3))
#define S1(x) XOR(XOR(ROR(x,17),ROR(x,19)),SHR(x,10))
#define BS0(x) XOR(XOR(ROR(x,2),ROR(x,13)),ROR(x,22))
#define BS1(x) XOR(XOR(ROR(x,6),ROR(x,11)),ROR(x,25))

AVX_TGT static inline void avx_compress(V8 st[8], const V8 in[16]) {
  V8 w[16];
  for (int i=0;i<16;i++) w[i]=in[i];
  V8 a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7];
  for (int i=0;i<64;i++) {
    V8 wi;
    if (i<16) wi=w[i];
    else {
      wi = ADD(ADD(S1(w[(i-2)&15]), w[(i-7)&15]), ADD(S0(w[(i-15)&15]), w[i&15]));
      w[i&15]=wi;
    }
    V8 ch = XOR(g, AND(e, XOR(f,g)));
    V8 t1 = ADD(ADD(h, BS1(e)), ADD(ADD(ch, _mm256_set1_epi32((int)K[i])), wi));
    V8 maj = OR(AND(a,b), AND(c, OR(a,b)));
    V8 t2 = ADD(BS0(a), maj);
    h=g; g=f; f=e; e=ADD(d,t1); d=c; c=b; b=a; a=ADD(t1,t2);
  }
  st[0]=ADD(st[0],a);st[1]=ADD(st[1],b);st[2]=ADD(st[2],c);st[3]=ADD(st[3],d);
  st[4]=ADD(st[4],e);st[5]=ADD(st[5],f);st[6]=ADD(st[6],g);st[7]=ADD(st[7],h);
}
AVX_TGT static uint32_t scan_avx2(scan_ctx *c, uint32_t start, uint32_t count) {
  uint32_t i = 0;
  const V8 zero = _mm256_setzero_si256();
  V8 midv[8], ivv[8], tailv[3];
  for (int k=0;k<8;k++) { midv[k]=_mm256_set1_epi32((int)c->mid[k]); ivv[k]=_mm256_set1_epi32((int)IV[k]); }
  for (int k=0;k<3;k++) tailv[k]=_mm256_set1_epi32((int)c->tail[k]);
  const V8 lane = _mm256_setr_epi32(0,1,2,3,4,5,6,7);
  const __m256i bswapmask = _mm256_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12,
                                             3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
  for (; i + 8 <= count; i += 8) {
    V8 w[16], st[8];
    V8 nonce = _mm256_add_epi32(_mm256_set1_epi32((int)(start+i)), lane);
    w[0]=tailv[0]; w[1]=tailv[1]; w[2]=tailv[2];
    w[3]=_mm256_shuffle_epi8(nonce, bswapmask);
    w[4]=_mm256_set1_epi32((int)0x80000000u);
    for (int k=5;k<15;k++) w[k]=zero;
    w[15]=_mm256_set1_epi32(640);
    for (int k=0;k<8;k++) st[k]=midv[k];
    avx_compress(st, w);
    for (int k=0;k<8;k++) w[k]=st[k];
    w[8]=_mm256_set1_epi32((int)0x80000000u);
    for (int k=9;k<15;k++) w[k]=zero;
    w[15]=_mm256_set1_epi32(256);
    for (int k=0;k<8;k++) st[k]=ivv[k];
    avx_compress(st, w);
    /* top 32 bits of the LE digest value = bswap(state[7]); unsigned x <= thr  <=>  min(x,thr)==x */
    V8 top = _mm256_shuffle_epi8(st[7], bswapmask);
    V8 thr = _mm256_set1_epi32((int)c->thr);
    int mask = _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_min_epu32(top,thr), top)));
    if (mask) {
      for (int l=0;l<8;l++) if (mask & (1<<l)) {
        if (exact_check(c, start+i+(uint32_t)l)) return i + (uint32_t)l + 1;
      }
    }
  }
  uint32_t used = scan_scalar(c, start + i, count - i);
  return i + used;
}

/* ------------------------------------------------------------ CPU detection */
static void cpuid_(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *cx, uint32_t *d) {
  uint32_t ea, eb, ec, ed;
  __asm__ volatile("cpuid" : "=a"(ea),"=b"(eb),"=c"(ec),"=d"(ed) : "a"(leaf),"c"(sub));
  *a=ea;*b=eb;*cx=ec;*d=ed;
}
static uint32_t detect_features(void) {   /* bit0 sha-ni usable, bit1 avx2 usable */
  uint32_t a,b,cx,d,maxleaf,feat=0;
  cpuid_(0,0,&maxleaf,&b,&cx,&d);
  if (maxleaf < 7) return 0;
  cpuid_(1,0,&a,&b,&cx,&d);
  int ssse3=(cx>>9)&1, sse41=(cx>>19)&1, osxsave=(cx>>27)&1, avx=(cx>>28)&1;
  uint32_t b7,c7,d7,a7;
  cpuid_(7,0,&a7,&b7,&c7,&d7);
  if (((b7>>29)&1) && ssse3 && sse41) feat |= 1u;           /* SHA */
  if (osxsave && avx && ((b7>>5)&1)) {
    uint32_t xlo, xhi;
    __asm__ volatile("xgetbv" : "=a"(xlo),"=d"(xhi) : "c"(0));
    if ((xlo & 6u) == 6u) feat |= 2u;                       /* OS saves YMM state */
  }
  return feat;
}
#else
static uint32_t detect_features(void) { return 0; }
#endif

static int g_engine = -1;
static int auto_engine(void) {
  uint32_t f = detect_features();
  if (f & 1u) return ENG_SHANI;
  if (f & 2u) return ENG_AVX2;
  return ENG_SCALAR;
}
static int engine_ok(int e) {
  uint32_t f = detect_features();
  if (e == ENG_SCALAR) return 1;
  if (e == ENG_SHANI) return (f & 1u) != 0;
  if (e == ENG_AVX2) return (f & 2u) != 0;
  return 0;
}

/* ---- exported API ---- */
CRS_EXPORT int crs_abi_version(void) { return 2; }
/* bit0 = SHA-NI usable, bit1 = AVX2 usable */
CRS_EXPORT uint32_t crs_cpu_features(void) { return detect_features(); }
/* Active engine id (auto-detected on first use). */
CRS_EXPORT int crs_engine(void) { if (g_engine < 0) g_engine = auto_engine(); return g_engine; }
/* Force an engine (0 scalar, 1 sha-ni, 2 avx2, -1 auto). Returns the engine now active, or -1 if unsupported here. */
CRS_EXPORT int crs_set_engine(int e) {
  if (e < 0) { g_engine = auto_engine(); return g_engine; }
  if (!engine_ok(e)) return -1;
  g_engine = e; return e;
}

CRS_EXPORT void crs_hash_nonce(const uint8_t *prefix76, uint32_t nonce, uint8_t *digest32) {
  uint32_t mid[8], tail[3], st[8];
  midstate(prefix76, mid);
  for (int i=0;i<3;i++) tail[i]=rd_be32(prefix76+64+4*i);
  hash_scalar(mid, tail, nonce, st);
  state_to_digest(st, digest32);
}

/* Returns number of nonces actually processed (<= count). Hits written to out_nonces,
 * at most max_hits. If the buffer fills, scanning stops right after that nonce and the
 * caller continues from there, so no qualifying nonce is dropped. out_best_digest32
 * receives the smallest digest seen (0xff..ff when count is zero).
 */
CRS_EXPORT uint32_t crs_scan(const uint8_t *prefix76, uint32_t start_nonce, uint32_t count,
                const uint8_t *target_digest_le, uint32_t *out_nonces, uint32_t max_hits,
                uint32_t *out_hit_count, uint8_t *out_best_digest32) {
  scan_ctx c;
  uint32_t i, processed;
  *out_hit_count = 0;
  for (i=0;i<32;i++) out_best_digest32[i]=0xff;
  if (!count || !max_hits) return 0;
  midstate(prefix76, c.mid);
  for (i=0;i<3;i++) c.tail[i]=rd_be32(prefix76+64+4*i);
  c.target = target_digest_le;
  for (i=0;i<32;i++) c.best[i]=0xff;
  c.hits = out_nonces; c.max_hits = max_hits; c.found = 0;
  c.target_top = digest_top32(target_digest_le);
  refresh_thr(&c);
  int eng = crs_engine();
#ifdef CRS_X86
  if (eng == ENG_SHANI)      processed = scan_shani(&c, start_nonce, count);
  else if (eng == ENG_AVX2)  processed = scan_avx2(&c, start_nonce, count);
  else
#endif
    processed = scan_scalar(&c, start_nonce, count);
  for (i=0;i<32;i++) out_best_digest32[i]=c.best[i];
  *out_hit_count = c.found;
  return processed;
}
