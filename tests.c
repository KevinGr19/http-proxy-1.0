#include "http.h"
#include "header_list.h"
#include "logging.h"

#include <stdio.h>
#include <strings.h>

int success = 0;
int total_tests = 0;

#define assert(func, name, condition) do{\
    if(!(condition)) printf("- Error for test \"%s.%s\" (%s:%d)\n", #func, name, __FILE_NAME__, __LINE__);\
    else success++;\
    total_tests++;\
}while(0)

void tests_http(void);
void tests_header_list(void);

int main(void){
    log_init(stdout, INFO);
    tests_http();
    tests_header_list();
    
    printf(">> Successful tests: %d/%d\n", success, total_tests);
    return 0;
}

void tests_http(void){
    int rc;
    rc = http_search_header_param("test2", "test2");
    assert(http_search_header_param, "present_only_one", rc == 0);

    rc = http_search_header_param("test, test1, test2, test3", "test4");
    assert(http_search_header_param, "not_present", rc == -1);

    rc = http_search_header_param("test1, test2, test3", "test1");
    assert(http_search_header_param, "present_first", rc == 0);

    rc = http_search_header_param("test1, test2, test3", "test2");
    assert(http_search_header_param, "present_mid", rc == 0);

    rc = http_search_header_param("test1, test2, test3", "test3");
    assert(http_search_header_param, "present_last", rc == 0);

    rc = http_search_header_param("test1, test2=value, test3", "test2");
    assert(http_search_header_param, "present_with_value", rc == 0);

    rc = http_search_header_param("test1, value=test2, test3", "test2");
    assert(http_search_header_param, "not_present_with_value_lookalike", rc == -1);

    rc = http_search_header_param("test1, test23, test3", "test2");
    assert(http_search_header_param, "not_present_with_param_starting_lookalike", rc == -1);

    rc = http_search_header_param("test1, test2=\"value\", test3", "test3");
    assert(http_search_header_param, "present_with_quoted_value", rc == 0);

    rc = http_search_header_param("test1, test2=\"value, test4\", test3", "test3");
    assert(http_search_header_param, "present_with_quoted_value_comma", rc == 0);

    rc = http_search_header_param("test1, test2=\"value, test4\", test3", "test4");
    assert(http_search_header_param, "not_present_with_quoted_value_comma_and_lookalike", rc == -1);
}

void tests_header_list(void){
    header_list* list = header_list_create();
    header_list_addraw(list, "Host", "localhost");
    header_list_addfmt(list, HDR_CONTENT_LENGTH, "%zu", 140);
    header_list_addraw(list, "eTaG", "EF1574D10C");

    header_list* update = header_list_create();
    header_list_addraw(update, "ETag", "abcdefghijk");
    header_list_addraw(update, "content-length", "250");
    header_list_addraw(update, "Accept", "*/*");

    header_list_update(list, update);

    heap_buf hb;
    heap_buf_init(&hb);
    header_list_write_buf(&hb, list);

    static const char* expected = 
        "Host: localhost\r\n"
        "Content-Length: 250\r\n"
        "ETag: abcdefghijk\r\n"
        "\r\n";

    debug_tr("output=\n%*s", hb.size, hb.buf);
    assert(header_list_update, "correct", strlen(expected) == hb.size && strncasecmp(hb.buf, expected, hb.size) == 0);
    header_list_free(list);
}