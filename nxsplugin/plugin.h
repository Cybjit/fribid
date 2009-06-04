#ifndef __PLUGIN_H__
#define __PLUGIN_H__

typedef enum {
    PT_Version,
    PT_Authentication,
} PluginType;

typedef enum {
    PE_OK = 0,
    PE_UnknownError = 1, // Maybe this is used for something else in the original plugin?
} PluginError;

typedef struct {
    PluginType type;
    
    char *url;
    char *hostname;
    char *ip;
    PluginError lastError;
    
    union {
        struct {
            /* Input parameters */
            char *challenge;
            char *policys;
            /* Output parameters */
            char *signature;
        } auth;
    } info;
} Plugin;

/* Implementation-specific initialization */
void bankid_init();
void bankid_shutdown();

/* Plugin creation */
Plugin *plugin_new(PluginType pluginType, const char *url,
                   const char *hostname, const char *ip);
void plugin_free(Plugin *plugin);

/* Javascript API */
char *version_getVersion(Plugin *plugin);

char *auth_getParam(Plugin *plugin, const char *name);
bool auth_setParam(Plugin *plugin, const char *name, const char *value);
int auth_performAction(Plugin *plugin, const char *action);
int auth_performAction_Authenticate(Plugin *plugin);
int auth_getLastError(Plugin *plugin);
// TODO more functions...

#endif


