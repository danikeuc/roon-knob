#include "playlist_admin_dial.h"
#include "playlist_client_dial.h"
#include "cJSON.h"
#define JSMN_STATIC
#include "third_party/jsmn.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <ctype.h>
#include <math.h>
#ifndef PLAYLIST_HOST_TEST
#include "nvs.h"
static const char private_namespace[] = "playlist_priv";
_Static_assert(sizeof private_namespace <= NVS_NS_NAME_MAX_SIZE, "playlist NVS namespace exceeds IDF limit");
#endif
static atomic_uint private_generation;
uint32_t playlist_private_generation(void){return atomic_load(&private_generation);}
/* Depth, grammar, token count, exact raw keys and embedded NUL are bounded before
 * recursive cJSON. Escaped key aliases are rejected rather than normalized. */
void *playlist_json_parse(const char *text,size_t length,size_t maximum) {
 if(!text||!length||length>maximum||memchr(text,0,length))return NULL;
 jsmntok_t *tokens=calloc(768,sizeof *tokens);if(!tokens)return NULL;jsmn_parser p;jsmn_init(&p);
 int count=jsmn_parse(&p,text,length,tokens,768);
 if(count<=0||tokens[0].type!=JSMN_OBJECT)goto invalid;
 unsigned depth=0;bool quoted=false,escaped=false;
 for(size_t i=0;i<length;i++) {
  char c=text[i];
  if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}
  else if(c=='"')quoted=true;
  else if(c=='{'||c=='['){if(++depth>4)goto invalid;}
  else if(c=='}'||c==']'){if(!depth)goto invalid;depth--;}
 }
 if(depth||quoted)goto invalid;
 for(int i=0;i<count;i++) {
  jsmntok_t *t=&tokens[i];
  if(t->type==JSMN_STRING) {
   for(int j=t->start;j<t->end;j++)if(text[j]=='\\'&&j+5<t->end&&text[j+1]=='u'&&!memcmp(text+j+2,"0000",4))goto invalid;
   size_t after=(size_t)t->end+1;while(after<length&&isspace((unsigned char)text[after]))after++;
   if(after<length&&text[after]==':'&&memchr(text+t->start,'\\',(size_t)(t->end-t->start)))goto invalid;
  }
  if(t->type==JSMN_PRIMITIVE) {
   size_t n=(size_t)(t->end-t->start);const char *v=text+t->start;
   if((n==4&&(!memcmp(v,"true",4)||!memcmp(v,"null",4)))||(n==5&&!memcmp(v,"false",5)))continue;
   /* This namespace uses unsigned integral JSON numbers only. */
   if(!n||(n>1&&v[0]=='0'))goto invalid;
   uint64_t value=0;
   for(size_t j=0;j<n;j++){if(v[j]<'0'||v[j]>'9'||value>(PLAYLIST_NUMBER_MAX-(unsigned)(v[j]-'0'))/10)goto invalid;value=value*10+(unsigned)(v[j]-'0');}
  }
 }
 const char *end=NULL;cJSON *json=cJSON_ParseWithLengthOpts(text,length,&end,false);
 if(!json)goto invalid;
 while(end<text+length&&isspace((unsigned char)*end))end++;
 if(end!=text+length){cJSON_Delete(json);goto invalid;}free(tokens);return json;
invalid:free(tokens);return NULL;
}
static bool keys(const cJSON *object,const char *const *names,unsigned count) {
 if(!cJSON_IsObject(object))return false;
 unsigned seen=0;
 for(cJSON *item=object->child;item;item=item->next){unsigned bit=0;for(unsigned i=0;i<count;i++)if(item->string&&!strcmp(item->string,names[i]))bit=1u<<i;if(!bit||(seen&bit))return false;seen|=bit;}
 return seen==((1u<<count)-1);
}
static cJSON *get(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static bool number(const cJSON *j,uint64_t *out){if(!cJSON_IsNumber(j)||!isfinite(j->valuedouble)||j->valuedouble<0||j->valuedouble>PLAYLIST_NUMBER_MAX||floor(j->valuedouble)!=j->valuedouble)return false;*out=(uint64_t)j->valuedouble;return true;}
static bool text(const cJSON *j,char *out,size_t cap,size_t max,bool empty){return cJSON_IsString(j)&& (empty||*j->valuestring)&&strlen(j->valuestring)<cap&&playlist_label_copy(out,cap,j->valuestring,max);}
bool playlist_favorites_parse(const char *body,size_t len,playlist_favorites_t *out) {
 static const char *const top[]={"revision","core_epoch","zone_id","zone_name","favorites"},*const entry[]={"id","title","available","reason"};
 cJSON *j=playlist_json_parse(body,len,PLAYLIST_FAVORITES_MAX);playlist_favorites_t f={0};char label[257];
 bool ok=keys(j,top,5)&&number(get(j,"revision"),&f.revision)&&number(get(j,"core_epoch"),&f.core_epoch)&&text(get(j,"zone_id"),f.zone_id,sizeof f.zone_id,128,false)&&text(get(j,"zone_name"),label,sizeof label,256,true)&&playlist_label_copy(f.zone_name,sizeof f.zone_name,label,256);
 cJSON *array=get(j,"favorites");int count=cJSON_GetArraySize(array);ok=ok&&cJSON_IsArray(array)&&count>=0&&count<=5;
 for(int i=0;ok&&i<count;i++) {
  cJSON *item=cJSON_GetArrayItem(array,i);playlist_favorite_t *e=&f.entries[i];
  ok=keys(item,entry,4)&&text(get(item,"id"),e->id,sizeof e->id,128,false)&&text(get(item,"title"),label,sizeof label,256,true)&&playlist_label_copy(e->title,sizeof e->title,label,256)&&cJSON_IsBool(get(item,"available"));
  e->available=cJSON_IsTrue(get(item,"available"));cJSON *reason=get(item,"reason");
  if(cJSON_IsString(reason)){ok=ok&&text(reason,label,sizeof label,256,true)&&playlist_label_copy(e->reason,sizeof e->reason,label,256);}else ok=ok&&cJSON_IsNull(reason);
  for(int n=0;ok&&n<i;n++)if(!strcmp(e->id,f.entries[n].id))ok=false;
 }
 f.count=(unsigned)count;cJSON_Delete(j);if(ok&&out)*out=f;return ok;
}
bool playlist_write_valid(const char *body,size_t len) {
 static const char *const names[]={"expected_revision","core_epoch","items"};
 cJSON *j=playlist_json_parse(body,len,PLAYLIST_BODY_MAX);uint64_t number_value;char ids[5][129];
 bool ok=keys(j,names,3)&&number(get(j,"expected_revision"),&number_value)&&number(get(j,"core_epoch"),&number_value);
 cJSON *array=get(j,"items");int count=cJSON_GetArraySize(array);ok=ok&&cJSON_IsArray(array)&&count<=5;
 for(int i=0;ok&&i<count;i++) {
  cJSON *e=cJSON_GetArrayItem(array,i);cJSON *selector=cJSON_IsObject(e)?e->child:NULL;
  ok=selector&&!selector->next&&(!strcmp(selector->string,"favorite_id")||!strcmp(selector->string,"selection_token"))&&text(selector,ids[i],129,128,false);
  for(int n=0;ok&&n<i;n++)if(!strcmp(ids[i],ids[n]))ok=false;
 }
 cJSON_Delete(j);return ok;
}
bool playlist_catalog_valid(const char *body,size_t len) {
 static const char *const names[]={"core_epoch","catalog_revision","items","next_cursor"},*const entry[]={"selection_token","title","subtitle"};
 cJSON *j=playlist_json_parse(body,len,PLAYLIST_CATALOG_MAX);uint64_t value;char t[257];
 bool ok=keys(j,names,4)&&number(get(j,"core_epoch"),&value)&&number(get(j,"catalog_revision"),&value);
 cJSON *a=get(j,"items");ok=ok&&cJSON_IsArray(a)&&cJSON_GetArraySize(a)<=50;
 for(int i=0;ok&&i<cJSON_GetArraySize(a);i++){cJSON *e=cJSON_GetArrayItem(a,i);ok=keys(e,entry,3)&&text(get(e,"selection_token"),t,129,128,false)&&text(get(e,"title"),t,257,256,true);cJSON *s=get(e,"subtitle");ok=ok&&(cJSON_IsNull(s)||text(s,t,257,256,true));}
 cJSON *cursor=get(j,"next_cursor");ok=ok&&(cJSON_IsNull(cursor)||text(cursor,t,129,128,false));cJSON_Delete(j);return ok;
}
bool playlist_capabilities_valid(const char *body,size_t len) {
 static const char *const names[]={"version","max_favorites","replace_queue"};cJSON *j=playlist_json_parse(body,len,1024);uint64_t v,m;
 bool ok=keys(j,names,3)&&number(get(j,"version"),&v)&&v==1&&number(get(j,"max_favorites"),&m)&&m==5&&cJSON_IsTrue(get(j,"replace_queue"));cJSON_Delete(j);return ok;
}
bool playlist_intent_parse(const char *body,size_t len,char *id,size_t cap,uint64_t *expires) {
 static const char *const names[]={"intent_id","expires_in_ms"};cJSON *j=playlist_json_parse(body,len,1024);
 bool ok=keys(j,names,2)&&text(get(j,"intent_id"),id,cap,128,false)&&number(get(j,"expires_in_ms"),expires)&&*expires>0&&*expires<=10000;cJSON_Delete(j);return ok;
}
bool playlist_receipt_parse(const char *body,size_t len,const char *id,const char *zone,int *status) {
 static const char *const names[]={"request_id","status","zone_id","error"};cJSON *j=playlist_json_parse(body,len,2048);char request[129]={0},target[129]={0},state[32]={0},error[257];
 bool with_error=get(j,"error")!=NULL;
 bool ok=keys(j,names,with_error?4:3)&&text(get(j,"request_id"),request,129,128,false)&&!strcmp(request,id)&&text(get(j,"zone_id"),target,129,128,false)&&!strcmp(target,zone)&&text(get(j,"status"),state,32,31,false);
 if(with_error)ok=ok&&(cJSON_IsNull(get(j,"error"))||text(get(j,"error"),error,257,256,true));
 *status=!strcmp(state,"accepted")?PLAYLIST_CLIENT_ACCEPTED:!strcmp(state,"playing")?PLAYLIST_CLIENT_PLAYING:!strcmp(state,"failed")?PLAYLIST_CLIENT_ERROR:!strcmp(state,"unknown")?PLAYLIST_CLIENT_UNKNOWN:0;
 ok=ok&&*status;cJSON_Delete(j);return ok;
}
#define PRIVATE_CAP 260
#ifdef PLAYLIST_HOST_TEST
static char private_blob[PRIVATE_CAP];
#endif
bool playlist_private_save(const char *base,const char *token) {
 if(!base||!token)return false;
 if(!*token)return true;
 size_t n=strlen(base),m=strlen(token);if(n>128||n<8||strncmp(base,"http://",7)||m>128||!m)return false;
 for(size_t i=0;i<n;i++)if((unsigned char)base[i]<=32||base[i]=='@'||base[i]=='?'||base[i]=='#')return false;
 for(size_t i=0;i<m;i++)if((unsigned char)token[i]<33||(unsigned char)token[i]>126)return false;
 char blob[PRIVATE_CAP],verify[129];snprintf(blob,sizeof blob,"%s\n%s",base,token);bool ok=false;
 atomic_fetch_add(&private_generation,1);
#ifdef PLAYLIST_HOST_TEST
 strcpy(private_blob,blob);ok=true;
#else
 nvs_handle_t handle;if(nvs_open(private_namespace,NVS_READWRITE,&handle)==ESP_OK){ok=nvs_set_blob(handle,"credential",blob,strlen(blob)+1)==ESP_OK&&nvs_commit(handle)==ESP_OK;nvs_close(handle);}
#endif
 ok=ok&&playlist_private_load(base,verify,sizeof verify)&&!strcmp(token,verify);memset(blob,0,sizeof blob);memset(verify,0,sizeof verify);atomic_fetch_add(&private_generation,1);return ok;
}
bool playlist_private_load(const char *base,char *token,size_t cap) {
 if(!base||!token||cap<129)return false;
 token[0]=0;char blob[PRIVATE_CAP]={0};bool ok=false;
#ifdef PLAYLIST_HOST_TEST
 strcpy(blob,private_blob);ok=*blob!=0;
#else
 nvs_handle_t handle;if(nvs_open(private_namespace,NVS_READONLY,&handle)==ESP_OK){size_t size=sizeof blob;ok=nvs_get_blob(handle,"credential",blob,&size)==ESP_OK&&size>0&&size<=sizeof blob&&blob[size-1]==0&&strlen(blob)+1==size;nvs_close(handle);}
#endif
 char *p=ok?strchr(blob,'\n'):NULL;if(p){*p++=0;ok=!strcmp(base,blob)&&strlen(p)<cap;if(ok)strcpy(token,p);}else ok=false;memset(blob,0,sizeof blob);return ok;
}
int playlist_admin_get(bool catalog,const char *cursor,char **json,size_t *length) {
 char path[256];if(cursor&&strlen(cursor)>128)return 422;
 if(cursor)for(const char *p=cursor;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')return 422;
 snprintf(path,sizeof path,catalog?"/dial/v1/playlists%s%s":"/dial/v1/favorites",cursor&&*cursor?"?cursor=":"",cursor?cursor:"");
 return playlist_client_admin("GET",path,NULL,catalog?PLAYLIST_CATALOG_MAX:PLAYLIST_FAVORITES_MAX,json,length);
}
int playlist_admin_put(const char *body,size_t length,char **json,size_t *out_length) {
 if(!playlist_write_valid(body,length))return 422;
 char *reply=NULL;size_t reply_len=0;int status=playlist_client_admin("PUT","/dial/v1/favorites",body,PLAYLIST_FAVORITES_MAX,&reply,&reply_len);
 if(status!=200){free(reply);return status;}
 playlist_favorites_t saved,readback;bool valid=playlist_favorites_parse(reply,reply_len,&saved);free(reply);
 cJSON *write=playlist_json_parse(body,length,PLAYLIST_BODY_MAX);uint64_t expected=0,epoch=0;
 valid=valid&&write&&number(get(write,"expected_revision"),&expected)&&number(get(write,"core_epoch"),&epoch)&&expected<PLAYLIST_NUMBER_MAX&&saved.revision==expected+1&&saved.core_epoch==epoch&&saved.count==(unsigned)cJSON_GetArraySize(get(write,"items"));
 for(unsigned i=0;valid&&i<saved.count;i++){cJSON *id=get(cJSON_GetArrayItem(get(write,"items"),(int)i),"favorite_id");if(id)valid=cJSON_IsString(id)&&!strcmp(id->valuestring,saved.entries[i].id);}
 cJSON_Delete(write);
 status=playlist_admin_get(false,NULL,json,out_length);
 valid=valid&&status==200&&playlist_favorites_parse(*json,*out_length,&readback)&&saved.revision==readback.revision&&saved.core_epoch==readback.core_epoch&&saved.count==readback.count&&!strcmp(saved.zone_id,readback.zone_id);
 for(unsigned i=0;valid&&i<saved.count;i++)valid=!strcmp(saved.entries[i].id,readback.entries[i].id);
 if(!valid){free(*json);*json=NULL;*out_length=0;return 503;}return 200;
}
