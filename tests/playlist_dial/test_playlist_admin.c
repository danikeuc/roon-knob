#include "playlist_admin_dial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 playlist_favorites_t f;
 const char *valid="{\"revision\":3,\"core_epoch\":7,\"zone_id\":\"sauna\",\"zone_name\":\"Sauna\",\"favorites\":[{\"id\":\"opaque\",\"title\":\"<img src=x onerror=evil()>\",\"available\":true,\"reason\":null}]}";
 assert(playlist_favorites_parse(valid,strlen(valid),&f)&&f.count==1&&!strcmp(f.entries[0].id,"opaque"));
 assert(!playlist_favorites_parse("{\"revision\\u0000evil\":3}",24,&f));
 assert(!playlist_write_valid("{\"expected_revision\":3,\"core_epoch\":7,\"items\":[{\"favorite_id\":\"x\",\"selection_token\":\"y\"}]}",92));
 const char *write="{\"expected_revision\":3,\"core_epoch\":7,\"items\":[{\"favorite_id\":\"x\"}]}";
 assert(playlist_write_valid(write,strlen(write)));
 assert(!playlist_write_valid("{\"expected_revision\":3e0,\"core_epoch\":7,\"items\":[]}",53));
 char label[5];assert(playlist_label_copy(label,sizeof label,"€€",256)&&!strcmp(label,"€"));
 assert(!playlist_label_copy(label,sizeof label,"\xc0\xaf",256));
 char token[129];assert(playlist_private_save("http://bridge/roon","synthetic-private"));assert(playlist_private_save("http://bridge/roon",""));assert(playlist_private_load("http://bridge/roon",token,sizeof token));assert(!playlist_private_load("http://other/roon",token,sizeof token));
 const char *aliases[]={"{\"expected_revision\\u0000evil\":3,\"core_epoch\":7,\"items\":[]}","{\"expected_revision\":3,\"expected_revision\":3,\"core_epoch\":7,\"items\":[]}","{\"expected_revision\":true,\"core_epoch\":7,\"items\":[]}","{\"expected_revision\":3,\"core_epoch\":7,\"items\":[{\"favorite_id\":\"x\\u0000evil\"}]}","{\"expected_revision\":3,\"core_epoch\":7,\"items\":[{\"favorite_id\":\"x\"},{\"favorite_id\":\"x\"}]}"};
 for(unsigned i=0;i<sizeof aliases/sizeof aliases[0];i++)assert(!playlist_write_valid(aliases[i],strlen(aliases[i])));
 char longid[130];memset(longid,'x',129);longid[129]=0;char body[1024];snprintf(body,sizeof body,"{\"revision\":3,\"core_epoch\":7,\"zone_id\":\"sauna\",\"zone_name\":\"Sauna\",\"favorites\":[{\"id\":\"%s\",\"title\":\"Music\",\"available\":true,\"reason\":null}]}",longid);assert(!playlist_favorites_parse(body,strlen(body),&f));
 char title[257];memset(title,'a',256);title[256]=0;snprintf(body,sizeof body,"{\"revision\":3,\"core_epoch\":7,\"zone_id\":\"sauna\",\"zone_name\":\"Sauna\",\"favorites\":[{\"id\":\"id\",\"title\":\"%s\",\"available\":true,\"reason\":null}]}",title);assert(playlist_favorites_parse(body,strlen(body),&f)&&strlen(f.entries[0].title)==96);
 puts("playlist admin parser/private URL binding PASS");
}
