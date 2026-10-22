#include "bds5g/b1c_navigation.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p,0777)
#endif

/* Enumerate transmitted fields once for the CSV header and values. */
#define INTEGER_FIELDS(X) X(PRN) X(WN) X(HOW) X(SOH) X(IODC) X(IODE) X(SatType) X(PageID) X(HS) X(DIF) X(SIF) X(AIF) X(SISMAI)
#define REAL_FIELDS(X) X(TOW) X(t_oe) X(deltaA) X(ADot) X(delta_n_0) X(delta_n_0Dot) X(M_0) X(e) X(omega) X(omega_0) X(i_0) X(omegaDot) X(i_0Dot) X(C_is) X(C_ic) X(C_rs) X(C_rc) X(C_us) X(C_uc) X(t_oc) X(a_0) X(a_1) X(a_2) X(T_GDB2ap) X(ISC_B1Cd) X(T_GDB1Cp) X(alpha1) X(alpha2) X(alpha3) X(alpha4) X(alpha5) X(alpha6) X(alpha7) X(alpha8) X(alpha9) X(A_0UTC) X(A_1UTC) X(A_2UTC) X(delta_t_LS) X(t_ot) X(WN_ot) X(WN_LSF) X(DN) X(delta_t_LSF) X(GNSS_ID) X(WN_0BGTO) X(t_0BGTO) X(A_0BGTO) X(A_1BGTO) X(A_2BGTO)
struct output {
  FILE *frames,*eph;
  unsigned long long candidates,decoded,bch_failed,crc_failed,fields_failed;
  int error;
};
static void received(const bds_b1c_nav_frame_t *f,void *context)
{
  struct output *o=context;
  const bds_b1c_ephemeris_t *e=&f->ephemeris;
  ++o->candidates;
  o->decoded+=e->valid;
  o->bch_failed+=f->status==BDS_B1C_NAV_BCH21||f->status==BDS_B1C_NAV_BCH51;
  o->crc_failed+=f->status==BDS_B1C_NAV_CRC2||f->status==BDS_B1C_NAV_CRC3;
  o->fields_failed+=f->status==BDS_B1C_NAV_PRN||f->status==BDS_B1C_NAV_FIELDS;
  fprintf(o->frames,"%d,%llu,%llu,%llu,%llu,%.17g,%d,%d,%s,%u,%u,%u,",
      f->prn,(unsigned long long)f->generation,(unsigned long long)f->first_epoch,
      (unsigned long long)f->absolute_sample,(unsigned long long)f->end_sample,
      f->sample_time_s,f->secondary_correlation,f->inverted,bds_b1c_nav_status_name(f->status),
      (unsigned)f->crc2_ok,(unsigned)f->crc3_ok,(unsigned)e->valid);
  if (e->valid) for (size_t j=0;j<878;++j) fputc('0'+f->bits[j],o->frames);
  fputc('\n',o->frames);
  if (e->valid) {
    fprintf(o->eph,"%d,%llu,%llu,%llu,%.17g",f->prn,(unsigned long long)f->generation,
        (unsigned long long)f->first_epoch,(unsigned long long)f->absolute_sample,f->sample_time_s);
#define PRINT_INTEGER(name) fprintf(o->eph,",%u",(unsigned)e->name);
    INTEGER_FIELDS(PRINT_INTEGER)
#undef PRINT_INTEGER
#define PRINT_REAL(name) fprintf(o->eph,",%.17g",e->name);
    REAL_FIELDS(PRINT_REAL)
#undef PRINT_REAL
    fputs(",1\n",o->eph);
  }
  if (ferror(o->frames)||ferror(o->eph)) o->error=1;
}
static int number(const char *s,double *out)
{
  char *end; errno=0; *out=strtod(s,&end);
  return !*s||*end||errno||!isfinite(*out) ? -1 : 0;
}
static int integer(const char *s,uint64_t *out)
{
  char *end; errno=0;
  if (!*s||*s<'0'||*s>'9') return -1;
  unsigned long long v=strtoull(s,&end,10);
  if (*end||errno) return -1;
  *out=(uint64_t)v; return 0;
}
static int split(char *line,char **tokens)
{
  size_t n=strcspn(line,"\r\n");
  if (line[n]=='\0'&&n>=16383) return -1;
  line[n]='\0'; int count=0;
  tokens[count++]=line;
  for (size_t j=0;j<n;++j) if (line[j]==',') {
    if (count==128) return -1;
    line[j]='\0'; tokens[count++]=line+j+1;
  }
  return count;
}
static int process(FILE *input,int prn,double fs,struct output *o,FILE *summary)
{
  char line[16384],*cols[128];
  const char *names[]={"epoch","generation","absolute_sample","sample_count","integration_s",
                       "data_i_p","pilot_q_p","state","valid"};
  int indices[9];
  unsigned long long rows=0,valid=0,breaks=0;
  uint64_t next_sample=0,next_epoch=0,generation=0;
  int pending=0,result=0;
  bds_b1c_navigator_t *nav=NULL;
  if (!fgets(line,sizeof(line),input)) return -1;
  int count=split(line,cols);
  if (count<1) return -1;
  for (size_t j=0;j<9;++j) {
    indices[j]=-1;
    for (int k=0;k<count;++k) if (!strcmp(cols[k],names[j])) {
      if (indices[j]>=0) return -1;
      indices[j]=k;
    }
    if (indices[j]<0) return -1;
  }
  if (bds_b1c_navigator_create(prn,fs,&nav)) return -1;
  unsigned long long before=o->candidates,decoded=o->decoded,bch=o->bch_failed,crc=o->crc_failed,fields=o->fields_failed;
  while (fgets(line,sizeof(line),input)) {
    bds_b1c_tracking_epoch_t e={0}; uint64_t sample_count,flag;
    ++rows;
    if (split(line,cols)!=count || integer(cols[indices[0]],&e.epoch) ||
        integer(cols[indices[1]],&e.generation) || integer(cols[indices[2]],&e.absolute_sample) ||
        integer(cols[indices[3]],&sample_count) || sample_count>SIZE_MAX ||
        number(cols[indices[4]],&e.integration_s) || number(cols[indices[5]],&e.data_prompt.i) ||
        number(cols[indices[6]],&e.pilot_prompt.q) || integer(cols[indices[8]],&flag) || flag>1) {
      result=-1; break;
    }
    e.prn=prn; e.sample_count=(size_t)sample_count; e.valid=(uint8_t)flag;
    const char *state=cols[indices[7]];
    if (!strcmp(state,"locked")) e.state=BDS_B1C_LOCKED;
    else if (!strcmp(state,"pull_in")) e.state=BDS_B1C_PULL_IN;
    else if (!strcmp(state,"lost")) e.state=BDS_B1C_LOST;
    else { result=-1; break; }
    if (e.valid&&e.state!=BDS_B1C_LOCKED) { result=-1; break; }
    if (pending&&(e.generation!=generation||e.epoch!=next_epoch||e.absolute_sample!=next_sample||!e.valid)) ++breaks;
    pending=e.valid;
    generation=e.generation; next_epoch=e.epoch+1; next_sample=e.absolute_sample+sample_count;
    valid+=e.valid;
    if (bds_b1c_navigator_push(nav,&e,received,o)||o->error) { result=-1; break; }
  }
  if (ferror(input)||!rows) result=-1;
  if (result) fprintf(stderr,"PRN %02d: invalid tracking CSV at row %llu\n",prn,rows);
  else fprintf(stderr,"PRN %02d: %llu epochs, %llu candidates, %llu decoded frames\n",prn,rows,o->candidates-before,o->decoded-decoded);
  fprintf(summary,"%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",prn,rows,valid,breaks,o->candidates-before,
      o->decoded-decoded,o->bch_failed-bch,o->crc_failed-crc,o->fields_failed-fields);
  bds_b1c_navigator_destroy(nav);
  return result;
}
static FILE *output_file(const char *directory,const char *name)
{
  char p[4096];
  if (snprintf(p,sizeof(p),"%s/%s",directory,name)>=(int)sizeof(p)) return NULL;
  return fopen(p,"wx"); /* No overwriting existing results. */
}
static int make_directory(const char *path)
{
  char p[4096]; size_t n=strlen(path);
  if (!n||n>=sizeof(p)) return -1;
  memcpy(p,path,n+1);
  struct stat st;
  /* Refuse an existing output directory, including symbolic aliases. */
  if (!stat(path,&st)||errno!=ENOENT) return -1;
  for (size_t j=1;j<=n;++j) if (!p[j]||p[j]=='/'||p[j]=='\\') {
    char saved=p[j]; p[j]=0;
    if (!(j==2&&p[1]==':') && MKDIR(p) && errno!=EEXIST) return -1;
    p[j]=saved;
  }
  return 0;
}
static void json_string(FILE *f,const char *s)
{
  fputc('"',f);
  for (;*s;++s) {
    unsigned char c=(unsigned char)*s;
    if (c=='"'||c=='\\') { fputc('\\',f); fputc(c,f); }
    else if (c<32) fprintf(f,"\\u%04x",c);
    else fputc(c,f);
  }
  fputc('"',f);
}
static int close_file(FILE *f)
{ if (!f) return 0; int result=ferror(f); return fclose(f)!=0||result; }
static void usage(const char *p)
{
  fprintf(stderr,"Usage: %s --tracking-dir DIR --sample-rate HZ --output-dir NEW_DIR\n"
    "Reads prn_01.csv .. prn_63.csv from bds-b1c-track (narrowband 10 ms epochs).\n"
    "Outputs frames.csv, ephemerides.csv, summary.csv, run.json.\n"
    "Exact secondary/BCH checks; systematic information + CRC, NO LDPC correction.\n"
    "Next: use checked frame timing and ephemerides for pseudorange development.\n",p);
}
int main(int argc,char **argv)
{
  const char *input=NULL,*directory=NULL; double fs=0;
  struct output o={0}; FILE *summary=NULL,*meta=NULL;
  int error=0,channels=0;
  for (int j=1;j<argc;++j) {
    if (!strcmp(argv[j],"--help")) { usage(argv[0]); return 0; }
    if (j+1>=argc) goto invalid;
    const char *name=argv[j],*value=argv[++j];
    if (!strcmp(name,"--tracking-dir")) input=value;
    else if (!strcmp(name,"--output-dir")) directory=value;
    else if (!strcmp(name,"--sample-rate")) { if (number(value,&fs)) goto invalid; }
    else goto invalid;
  }
  if (!input||!directory||fs<=0) goto invalid;
  struct stat st;
  if (stat(input,&st)||!(st.st_mode&S_IFDIR)) { perror("Tracking directory"); return 2; }
  if (make_directory(directory)) { fprintf(stderr,"Output must be a new directory: %s\n",directory); return 2; }
  o.frames=output_file(directory,"frames.csv"); o.eph=output_file(directory,"ephemerides.csv");
  summary=output_file(directory,"summary.csv"); meta=output_file(directory,"run.json");
  if (!o.frames||!o.eph||!summary||!meta) { error=3; goto done; }
  fputs("prn,generation,first_epoch,absolute_sample,end_sample,sample_time_s,secondary_correlation,inverted,status,crc2_ok,crc3_ok,valid,bits\n",o.frames);
  fputs("prn,generation,first_epoch,absolute_sample,sample_time_s",o.eph);
#define HEADER(name) fputs("," #name,o.eph);
  INTEGER_FIELDS(HEADER) REAL_FIELDS(HEADER)
#undef HEADER
  fputs(",valid\n",o.eph);
  fputs("prn,epochs,valid_epochs,continuity_breaks,secondary_candidates,decoded_frames,bch_failed,crc_failed,fields_failed\n",summary);
  for (int prn=1;prn<=63&&!error;++prn) {
    char p[4096];
    if (snprintf(p,sizeof(p),"%s/prn_%02d.csv",input,prn)>=(int)sizeof(p)) { error=2; break; }
    errno=0; FILE *csv=fopen(p,"r");
    if (!csv) { if (errno!=ENOENT) error=3; continue; }
    ++channels;
    if (process(csv,prn,fs,&o,summary)) error=3;
    if (fclose(csv)) error=3;
  }
  if (!channels) { fprintf(stderr,"No per-PRN tracking CSV files found\n"); error=2; }
done:
  if (close_file(o.frames)|close_file(o.eph)|close_file(summary)) error=3;
  if (meta) {
    fputs("{\n  \"schema_version\": 1,\n  \"tracking_directory\": ",meta); json_string(meta,input);
    fprintf(meta,",\n  \"sample_rate_hz\": %.17g,\n  \"secondary_max_errors\": 0,\n"
      "  \"ldpc_error_correction\": false,\n  \"channels\": %d,\n  \"decoded_frames\": %llu,\n"
      "  \"complete\": %s,\n  \"status_code\": %d\n}\n",fs,channels,o.decoded,error?"false":"true",error);
    if (close_file(meta)) error=3;
  }
  return error;
invalid:
  usage(argv[0]); return 2;
}
