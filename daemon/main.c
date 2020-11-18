#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h> 
#include <libmx-dx/dx_api.h>
//#include <libmx-event-agent/event_api.h>
#include <parson.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libmx-cmd-agent/http_server.h>


#define MYSQL_PROCOTOL 1
#define DEFAULT_PORT 8000 
#define MAXLINE 4096 
#define DATABASE_NAME "fieldbusprotocol"



// pre-define const value
#define PUBLISH_TOPIC "fieldbusprotocol/%s/%s"
char *MODULE_NAME = "fieldbusprotocol";
const char *CONFIG_PATH = "/etc/fieldbus/config.json";
const char *DB_PATH = "/etc/fieldbus/protocol.db";
static volatile int EXIT_SIGINT = 0;
static volatile int front_mode = 0;

//static pthread_t mysql_connent_thread;
static pthread_t loop_thread;
static pthread_t write_tags_loop_thread;


typedef struct
{
    char* tag_name;
    //char* op;
    //char* value_type;
} Tag;

typedef struct
{
    char *device_name;
    int poll_interval;
    int tag_count;
    Tag *tags;
} Device;

#ifdef MYSQL_PROCOTOL
#include <mariadb/mysql.h>

#define HOSTADDRESS "10.0.20.132"
#define USERNAME "root"
#define USERPWD "root"


MYSQL *g_connect = NULL;
unsigned int g_num_fields = 0;

int get_api_token(char *api){


    FILE* fp = fopen("/etc/app_setting/mx-api-token", "r");
    if (fp == NULL) {
        fp = fopen("/var/thingspro/data/mx-api-token", "r");
        if (fp == NULL) {
            return -1;
        }
    }

    size_t ret = fread(api, 1, 1024, fp);
    if (ret <= 0) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int connect_mysql_server(void){
	//printf("MySQL client version: %s\n", mysql_get_client_info());

	g_connect = mysql_init(NULL);
	if(g_connect == NULL){
		printf("MySQL client init fail, error:%s!\n", mysql_error(g_connect));
		return -1;
	} 
	//printf("MySQL client init OK\n");

    bool reconnect = true;
    mysql_options(g_connect, MYSQL_OPT_RECONNECT, &reconnect);


    
	while(mysql_real_connect(g_connect, HOSTADDRESS, USERNAME, USERPWD, NULL, 0, NULL, 0) == NULL){
		printf("MySQL client connect fail, error:%s!\n", mysql_error(g_connect));
        sleep(1);
		//mysql_close(g_connect);
		//return -1;
	} 
    
    
 
	printf("MySQL client connect OK\n");
	//printf("MySQL server address:%s\n", HOSTADDRESS);
	//printf("MySQL client username:%s\n", USERNAME);
    #if 1
    char *url = "http://172.31.8.1:59000/api/v1/events";
    char api_token[1024];
    memset(api_token, 0, sizeof(api_token));
    get_api_token(api_token);
    char *request_buf = "{\"category\":\"SouthApp\", \"event\":\"device connected\", \"origin\":\"fieldbusprotocol\", \"message\":\"device connected\", \"severity\":\"info\"}";
    uint32_t request_buf_len = strlen(request_buf);

    HTTP_STATUS status = http_req_send_with_token(url, HTTP_REQ_OP_POST, "application/json", api_token, request_buf, request_buf_len, NULL, NULL);
    //printf("%d\n", status);
    #endif
    
	return 0;
}
#endif

static void sigint_handler(int sig)
{
    printf("%s %d sig=%d\n", __FUNCTION__, __LINE__, sig);
    EXIT_SIGINT = 1;
}

int arg_setup(int argc, char **argv)
{
    char c;
    char optstring[] = "f";
    
    while ((c = getopt(argc, argv, optstring)) != 0xFF)
    {
        switch (c)
        {
        case 'f':
            front_mode = 1;
            break;
        default:
            if (((int)c) < 0) return 0;
            break;
        }
    }
    return 0;
}

int parse_config(const char* config_path, Device **device_config) 
{
    Device *config = (Device*)calloc(1, sizeof(Device));
    JSON_Value *root_value;
    JSON_Array *tags_array;
    /* parsing json and validating output */
    root_value = json_parse_file(config_path);
    const char* device_name = json_object_get_string(json_object(root_value), "deviceName");
    config->device_name = (char*)calloc(1, strlen(device_name)+1);
    strcpy(config->device_name, device_name);

    config->poll_interval = (int)json_object_get_number(json_object(root_value), "pollingInterval");

    tags_array = json_object_get_array(json_object(root_value), "tags");
    size_t tag_count = json_array_get_count(tags_array);

    config->tags = (Tag*)calloc(1, sizeof(Tag) * tag_count);
    config->tag_count = tag_count;
    int idx;
    for (idx = 0; idx < tag_count; idx++) {
        JSON_Object* tag =  json_array_get_object(tags_array, idx);
        const char* tag_name = json_object_get_string(tag, "tagName");
        config->tags[idx].tag_name = (char*)calloc(1, strlen(tag_name)+1);
        strcpy( config->tags[idx].tag_name, tag_name);
    }
    json_value_free(root_value);
    *device_config = config;
    return 0;
}

void *write_tags_looping(void *data) {

    //JSON_Value *root_value;
    //JSON_Array *tags_array;
    
	MYSQL_ROW row;
	MYSQL_RES *res;
	unsigned int num_fields;
    char mysql_cmd[1024];


    int socket_fd;
    int connect_fd;  
    struct sockaddr_in servaddr; 
    char    buff[4096];  
    int     n;

    //AF_INET: ipV4
    //SOCK_STREAM: stream socket
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){  
        printf("create socket error\n");  
        return NULL;
    }
    //init
    memset(&servaddr, 0, sizeof(servaddr));  
    servaddr.sin_family = AF_INET;  
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);//ip
    servaddr.sin_port = htons(DEFAULT_PORT);//socket port  
    //bind  
    if(bind(socket_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1){ 
        printf("bind socket error\n");  
        return NULL;
    } 
    //listen
    if(listen(socket_fd, 10) == -1){  
        
        printf("listen socket error\n");  
        return NULL;
    }
    //accept  
    if((connect_fd = accept(socket_fd, (struct sockaddr*)NULL, NULL)) == -1){  
        printf("accept socket error\n");  
        return NULL;
    }  
    while(!EXIT_SIGINT){
        
        //receive message  
        memset(buff, 0, sizeof(buff));
        n = recv(connect_fd, buff, MAXLINE, 0);  
    
        //send  
        if(n > 0){
            buff[n] = '\0';  

            JSON_Value *receive_root_value;
            JSON_Value *send_root_value;
            
            receive_root_value = json_parse_string(buff);
            const char* method = json_object_get_string(json_object(receive_root_value), "method");
            const char* databaseName = json_object_get_string(json_object(receive_root_value), "databaseName");
            const char* tableName = json_object_get_string(json_object(receive_root_value), "tableName");
            const char* tagName = json_object_get_string(json_object(receive_root_value), "tagName");
            const char* tagValue = json_object_get_string(json_object(receive_root_value), "tagValue");

            if(mysql_select_db(g_connect, databaseName) != 0){
                printf("mysql select database %s error:%s!\n", DATABASE_NAME, mysql_error(g_connect));
                continue;
            }
           
            memset(mysql_cmd, 0, sizeof(mysql_cmd));
            snprintf(mysql_cmd, sizeof(mysql_cmd), "select %s from %s;", tagName, tableName);
            if(mysql_query(g_connect, mysql_cmd) != 0){
                printf("mysql_query fail, error:%s!\n", mysql_error(g_connect));
                continue;
            }
            if((res = mysql_store_result(g_connect)) == NULL){
                printf("mysql_store_result fail, error:%s!\n", mysql_error(g_connect));
                continue;
            }
            num_fields = mysql_num_fields(res);
            if(NULL != (row = mysql_fetch_row(res)))
            {   
#if 0
                while (NULL != (field = mysql_fetch_field(res)))
                {
                    printf("%s ", field->name);
            
                }
                printf("\n");
                int j;
                lengths = mysql_fetch_lengths(res);
                for (j = 0; j < num_fields; j++)
                {
                    printf("{%.*s}\t", (int) lengths[j], row[j] ? row[j] : "NULL");
                }
                printf("\n");
#endif                 
            }
            
            if(memcmp(method, "set", strlen("set")) == 0){
                if(strlen(row[num_fields-1]) != strlen(tagValue) || strncmp(row[num_fields-1], tagValue, strlen(tagValue)) != 0){
                    memset(mysql_cmd, 0, sizeof(mysql_cmd));
                    snprintf(mysql_cmd, sizeof(mysql_cmd), "update %s set %s=%s where %s=%s;",\
                    tableName, tagName, tagValue, tagName, row[num_fields-1]);
                    if(mysql_query(g_connect, mysql_cmd) != 0){
                        printf("mysql_query fail, error:%s!\n", mysql_error(g_connect));
                        continue;
                    }
                }
            } else if(memcmp(method, "get", strlen("get")) == 0){
                if(strlen(row[num_fields-1]) != strlen(tagValue) || strncmp(row[num_fields-1], tagValue, strlen(tagValue)) != 0){
                    tagValue = row[num_fields-1];
                }
            } 

            send_root_value = json_value_init_object();
            JSON_Object *root_object = json_value_get_object(send_root_value);

            char *serialized_string = NULL;
            //json_object_set_string(root_object, "method", "get");
            json_object_set_string(root_object, "databaseName", databaseName);
            json_object_set_string(root_object, "tableName", tableName);
            json_object_set_string(root_object, "tagName", tagName);
            json_object_set_string(root_object, "tagValue", tagValue);
            
            serialized_string = json_serialize_to_string_pretty(send_root_value);

            memset(buff, 0, sizeof(buff));
            strncpy(buff, serialized_string, sizeof(buff));
            n = strlen(buff);
            json_free_serialized_string(serialized_string);
            
            json_value_free(receive_root_value);
            json_value_free(send_root_value);


            
            if(send(connect_fd, buff, n, 0) < 0){
                perror("send error");  
            } 
            else {
                //printf("%s %d send ok [%s]\n", __FUNCTION__, __LINE__, buff);
            }
        } 
 
    }

    close(connect_fd); 
    close(socket_fd); 
    printf("write_tags_looping exit!\n");

    return NULL;
}

void dx_tag_proxy_callback(DX_TAG_OBJ* dx_data_obj, uint16_t obj_cnt, void* user_data) {

    printf("dx_data_obj->tag=%s\n", dx_data_obj->tag);
    printf("dx_data_obj->val_type=%d\n", dx_data_obj->val_type);
    
    printf("dx_data_obj->user_data=%s\n", (char *)dx_data_obj->user_data);

    printf("obj_cnt=%d\n", obj_cnt);
    printf("user_data=%s\n", (char *)user_data);


}



void *poll_data_looping(void *data) {



    Device *device_tags = (Device*)data;
    if (!device_tags) {
        return NULL;
    }

    DX_TAG_CLIENT *client = NULL;

    // init dx api as client
    client = dx_tag_client_init(MODULE_NAME, dx_tag_proxy_callback);
    if (!client) {
        printf("tag client init failed\n");      
        return NULL;
    }

    if(connect_mysql_server() != 0){
        return NULL;
    }

    #if 0
    char buff[2048];
    memset(buff, 0, sizeof(buff));

    int ret = dx_tag_sub(client, "/fieldbusprotocol/dbdevice/temperature", (void *)buff);
    printf("ret=%d\n",ret);
    #endif

	MYSQL_ROW row;
	int i;
	MYSQL_RES *res;
	unsigned int num_fields;
    char topic[128];
    
    DX_TAG_VALUE tag_value;
    int value_type = DX_TAG_VALUE_TYPE_INT32;

    char query_str_buff[128];

	if(mysql_select_db(g_connect, DATABASE_NAME) != 0){
		printf("mysql select database %s error:%s!\n", DATABASE_NAME, mysql_error(g_connect));
		mysql_close(g_connect);
		return NULL;
	}

    
    while(!EXIT_SIGINT) {
		for (i = 0; i < device_tags->tag_count; i++)      
		{
            memset(query_str_buff, 0, sizeof(query_str_buff));
            snprintf(query_str_buff, sizeof(query_str_buff), "select %s from dbdevice;", device_tags->tags[i].tag_name);
            if(mysql_query(g_connect, query_str_buff) != 0){
                //mysql_close(g_connect);
                printf("mysql_query fail, error:%s!\n", mysql_error(g_connect));
                continue;
                //return -1;
            }
            
            if((res = mysql_store_result(g_connect)) == NULL){
                //mysql_close(g_connect);
                printf("mysql_store_result fail, error:%s!\n", mysql_error(g_connect));
                //return -1; 
                continue;
            }
            
            num_fields = mysql_num_fields(res);
            if(NULL != (row = mysql_fetch_row(res)))
            {   
                #if 0
                while (NULL != (field = mysql_fetch_field(res)))
                {
                    printf("%s ", field->name);
            
                }
                printf("\n");
                int j;
                lengths = mysql_fetch_lengths(res);
                for (j = 0; j < num_fields; j++)
                {
                    printf("{%.*s}\t", (int) lengths[j], row[j] ? row[j] : "NULL");
                }
                printf("\n");
                #endif
              
            } else {
                printf("mysql_fetch_row fail, error:%s!\n", mysql_error(g_connect));
                mysql_free_result(res);
                continue;
            }
            mysql_free_result(res);

            
            memset(topic, 0, sizeof(topic));
            sprintf(topic, PUBLISH_TOPIC, device_tags->device_name, device_tags->tags[i].tag_name);
            value_type = DX_TAG_VALUE_TYPE_INT32;
            tag_value.i32 = atoi(row[num_fields-1]);
                        
            // do tag publish 
            struct timeval tv;
            gettimeofday(&tv,NULL);
            unsigned long time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
            int rc = dx_tag_pub(
                        client,
                        topic,
                        value_type,
                        tag_value,
                        time_in_micros);
            if (rc != 0)
                printf("tag publish failed(ret:%d)\n", rc);

            usleep(device_tags->poll_interval*1000);
		}
        
    }

    dx_tag_destroy(client);
    mysql_close(g_connect);
    
    printf("poll_data_looping exit!\n");
    return NULL;
}

int main(int argc, char *argv[])
{
    arg_setup(argc, argv);
    
    if (front_mode == 0) {
        daemon(0, 0);
    }
   
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    signal(SIGKILL, sigint_handler);

    // load config 
    Device *conf = NULL;
    parse_config(CONFIG_PATH, &conf);
    
    printf("parse device and tag successfully, tag count:%d\n", conf->tag_count);
    int i; 
    for (i = 0; i < conf->tag_count; i++) {
        printf("device name:%s, polling_interval:%d, tag name:%s\n", conf->device_name, conf->poll_interval, conf->tags[i].tag_name);
    }


    pthread_attr_t attr;
    pthread_attr_init (&attr);


    if (pthread_create(&loop_thread, NULL, poll_data_looping, (void*)conf) != 0) {
        printf("create poll data looping failed\n");
    }
    
    if (pthread_create(&write_tags_loop_thread, &attr, write_tags_looping, NULL) != 0) {
        printf("create write tags poll data looping failed\n");
    }

    
    while(!EXIT_SIGINT)
    {
        sleep(1);
    }
    pthread_join(loop_thread, NULL);
    //pthread_join(write_tags_loop_thread, NULL);
    printf("fiedlbus protocol deamon exit\n");
    return 0;
}
