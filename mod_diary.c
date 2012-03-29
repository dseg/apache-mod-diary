/*
**  mod_diary.c -- Apache sample diary module
**  [Autogenerated via ``apxs -n diary -g'']
**
**  To play with this sample module first compile it into a
**  DSO file and install it into Apache's modules directory
**  by running:
**
**    % ./autogen.sh
**    % ./configure --with-apache=<APACHE_DIR>  \
**        --with-discount=<DISCOUNT_DIR>  \
**        --with-clearsilver=<CLEARSILVER_DIR>
**    % make
**    # make install
**
**  Then activate it in Apache's httpd.conf file for instance
**  for the URL /diary in as follows:
**
**    #   httpd.conf
**    LoadModule diary_module modules/mod_diary.so
**    <Location /diary>
**      SetHandler diary
**      DiaryPath /path/to/diary
**      DiaryTitle Sample Diary
**    </Location>
**
**  Then after restarting Apache via
**
**    $ apachectl restart
**
**  you immediately can request the URL /diary and watch for the
**  output of this module. This can be achieved for instance via:
**
**    $ lynx -mime_header http://localhost/diary
**
**  The output should be similar to the following one:
**
**    HTTP/1.1 200 OK
**    Date: Tue, 31 Mar 1998 14:42:22 GMT
**    Server: Apache/1.3.4 (Unix)
**    Connection: close
**    Content-Type: text/html
**
**    The sample page from mod_diary.c
*/

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_log.h"
#include "http_core.h"

#include "ap_config.h"
#include "apr_strings.h"

#include "mkdio.h"
#include "ClearSilver.h"

module AP_MODULE_DECLARE_DATA diary_module;

typedef struct {
    apr_pool_t *pool;
    int init;
    const char *path;
    const char *uri;
    const char *title;
    const char *theme;
    const char *theme_index_cs;
    const char *index_hdf;
} diary_conf;

#define P(s) ap_rputs(s, r)

static void diary_init(diary_conf *conf)
{
    if(!conf->uri) {
        conf->uri = "";
    }

    if(!conf->title) {
        conf->title = "My Diary";
    }

    if(!conf->theme) {
        conf->theme = "default";
    }

    conf->theme_index_cs =
        apr_pstrcat(conf->pool, conf->path, "/themes/", conf->theme,
                    "/index.cst", NULL);

    conf->index_hdf = apr_pstrcat(conf->pool, conf->path, "/index.hdf", NULL);
    conf->init = 1;
}

static NEOERR *diary_cs_render_cb(void *ctx, char *s)
{
    ap_rputs(s, (request_rec *)ctx);
    return STATUS_OK;
}

static int diary_process_index(request_rec *r, diary_conf *conf)
{
    int ret;
    HDF *hdf;
    CSPARSE *cs;
    NEOERR *cs_err;
    STRING cs_err_str;

    hdf_init(&hdf);
    hdf_set_value(hdf, "diary.title", conf->title);
    hdf_set_value(hdf, "diary.uri", conf->uri);

    ret = hdf_read_file(hdf, conf->index_hdf);
    if(ret){
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "cannot read index.hdf. errno=%d", errno);
        hdf_destroy(&hdf);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    //hdf_dump(hdf, NULL);

    cs_err = cs_init(&cs, hdf);
    if(cs_err){
        string_init(&cs_err_str);
        nerr_error_string(cs_err, &cs_err_str);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "error at cs_init(): %s", cs_err_str.buf);
        // TODO: no need to free cs_err and cs_err_str?
        cs_destroy(&cs);
        hdf_destroy(&hdf);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    cgi_register_strfuncs(cs);

    cs_err = cs_parse_file(cs, conf->theme_index_cs);
    if(cs_err){
        string_init(&cs_err_str);
        nerr_error_string(cs_err, &cs_err_str);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "error in cs_parse_file(): %s", cs_err_str.buf);
        // TODO: no need to free cs_err and cs_err_str?
        cs_destroy(&cs);
        hdf_destroy(&hdf);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    r->content_type = "text/html";
    cs_render(cs, r, diary_cs_render_cb);

    cs_destroy(&cs);
    hdf_destroy(&hdf);
    return OK;
}


static int diary_process_entry(request_rec *r,
                               diary_conf *conf,
                               const char *filename)
{
    FILE *fp;
    CSPARSE *cs;
    NEOERR *cs_err;
    HDF *hdf;
    MMIOT *doc;
    char *title;
    char *author;
    char *date;
    int size;
    char *p;

    fp = fopen(filename, "r");
    if(fp == NULL){
        switch (errno) {
        case ENOENT:
            return HTTP_NOT_FOUND;
        case EACCES:
            return HTTP_FORBIDDEN;
        default:
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "diary_parse_entry error: errno=%d\n", errno);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }
    doc = mkd_in(fp, 0);
    fclose(fp);
    if (doc == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    title = mkd_doc_title(doc);
    if(title == NULL){
        title = "notitle";
    }
    date = mkd_doc_date(doc);
    author = mkd_doc_author(doc);

    mkd_compile(doc, MKD_TOC);
    if ((size = mkd_document(doc, &p)) == EOF) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    hdf_init(&hdf);

    hdf_read_file(hdf, conf->index_hdf);
    //hdf_dump(hdf, NULL);
    hdf_set_value(hdf, "diary.title", conf->title);
    hdf_set_value(hdf, "diary.uri", conf->uri);

    hdf_set_value(hdf, "entry.uri", r->uri);
    hdf_set_value(hdf, "entry.title", title);
    hdf_set_value(hdf, "entry.author", author);
    hdf_set_value(hdf, "entry.date", date);
    hdf_set_value(hdf, "entry.content", p);

    cs_err = cs_init(&cs, hdf);
    if(cs_err){
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    cgi_register_strfuncs(cs);
    mkd_cleanup(doc);
    cs_parse_file(cs, conf->theme_index_cs);

    r->content_type = "text/html";
    cs_render(cs, r, diary_cs_render_cb);

    hdf_destroy(&hdf);
    cs_destroy(&cs);
    return 0;
}

/* The diary handler */
static int diary_handler(request_rec *r)
{
    diary_conf *conf;
    int ret;
    char *filename;

    if (strcmp(r->handler, "diary")) {
        return DECLINED;
    }

    if (r->header_only) {
        return OK;
    }
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "diary_handler: %s", r->path_info);

    conf = (diary_conf *) ap_get_module_config(r->per_dir_config,
                                               &diary_module);
    if(!conf->init){
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "diary_init()");
        diary_init(conf);
    }
/*
    printf("r->uri: %s\n", r->uri);
    printf("r->filename: %s\n", r->filename);
    printf("r->canonical_filename: %s\n", r->canonical_filename);
    printf("r->path_info: %s\n", r->path_info);
    printf("r->content_type: %s\n", r->content_type);
    printf("conf->path: %s\n", conf->path);
*/
    if (!strcmp(r->path_info, "/")) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "diary_type_checker(): process index");
        return diary_process_index(r, conf);
    }else if(!strncmp(r->path_info, "/feed/", 6)){
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "diary_type_checker(): process feed");
        return OK;
    }

    r->path_info = NULL;
    ret = apr_stat(&r->finfo, r->filename, APR_FINFO_MIN, r->pool);
    /* see apr_file_info.h:apr_filetype_e */
    if(ret == 0 && r->finfo.filetype == APR_REG){
        return DECLINED;
    }

    filename = apr_pstrcat(r->pool, r->filename, ".md", NULL);
    ret = apr_stat(&r->finfo, filename, APR_FINFO_MIN, r->pool);
    if(!ret){
        ret = diary_process_entry(r, conf, filename);
        return OK;
    }

    return DECLINED;
}

static int diary_type_checker(request_rec *r)
{
    diary_conf *conf;
    conf = (diary_conf *)ap_get_module_config(r->per_dir_config,
                                              &diary_module);
    if(conf->path == NULL) {
        return DECLINED;
    }
    r->filename = apr_pstrcat(r->pool, conf->path, r->path_info, NULL);
    return DECLINED;
}

static void *diary_config(apr_pool_t *p, char *dummy)
{
    diary_conf *c = (diary_conf *) apr_pcalloc(p, sizeof(diary_conf));
    memset(c, 0, sizeof(diary_conf));
    c->pool = p;
    return (void *)c;
}

static const char *set_diary_path(cmd_parms * cmd, void *conf,
                                  const char *arg)
{
    diary_conf *c = (diary_conf *)conf;
    c->path = arg;
    return NULL;
}

static const char *set_diary_title(cmd_parms * cmd, void *conf,
                                   const char *arg)
{
    diary_conf *c = (diary_conf *)conf;
    c->title = arg;
    return NULL;
}

static const char *set_diary_uri(cmd_parms * cmd, void *conf,
                                 const char *arg)
{
    diary_conf *c = (diary_conf *)conf;
    c->uri = arg;
    return NULL;
}

static const char *set_diary_theme(cmd_parms *cmd, void *conf,
                                   const char *arg)
{
    diary_conf *c = (diary_conf *)conf;
    c->theme = arg;
    return NULL;
}

static const command_rec diary_cmds[] = {
    AP_INIT_TAKE1("DiaryPath", set_diary_path, NULL, OR_ALL,
                  "set DiaryPath"),
    AP_INIT_TAKE1("DiaryUri", set_diary_uri, NULL, OR_ALL,
                  "set DiaryUri"),
    AP_INIT_TAKE1("DiaryTitle", set_diary_title, NULL, OR_ALL,
                  "set DiaryTitle"),
    AP_INIT_TAKE1("DiaryTheme", set_diary_theme, NULL, OR_ALL,
                  "set DiaryTheme"),
    {NULL}
};

/*
static int diary_post_config(apr_pool_t *p, apr_pool_t *plog,
                             apr_pool_t *ptemp, server_rec *s)
{
    return 0;
}
*/

static void diary_register_hooks(apr_pool_t *p)
{
    //ap_hook_post_config(diary_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(diary_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_type_checker(diary_type_checker, NULL, NULL, APR_HOOK_FIRST);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA diary_module = {
    STANDARD20_MODULE_STUFF,
    diary_config,               /* create per-dir    config structures */
    NULL,                       /* merge  per-dir    config structures */
    NULL,                       /* create per-server config structures */
    NULL,                       /* merge  per-server config structures */
    diary_cmds,                 /* table of config file commands       */
    diary_register_hooks        /* register hooks                      */
};
