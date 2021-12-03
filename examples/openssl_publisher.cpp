
/**
 * @file
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <SSLPublisher.h>
#include <iostream>
#include <mqtt.h>

#include "templates/openssl_sockets.h"


/**
 * @brief The function that would be called whenever a PUBLISH is received.
 * 
 * @note This function is not used in this example. 
 */
void publish_callback(void** unused, struct mqtt_response_publish *published);

/**
 * A simple program to that publishes the current time whenever ENTER is pressed. 
 */
int main(int argc, const char *argv[]) 
{
    const char* addr= "127.0.0.1";
    const char* port = "8883";
    const char* topic= "/world";
    const char* ca_file= "/home/will/mqtt-certs/ca.crt";
    const char* cert_path= "/home/will/mqtt-certs/client.crt";
    const char* key_path= "/home/will/mqtt-certs/client.key";

    MQTT::SSLPublisher publisher(addr, port, publish_callback, ca_file, "", cert_path, key_path);

    /* start publishing the time */
    printf("%s is ready to begin publishing the time.\n", argv[0]);
    printf("Press ENTER to publish the current time.\n");
    printf("Press CTRL-D (or any other key) to exit.\n\n");
    while(fgetc(stdin) == '\n') {
        /* get the current time */
        time_t timer;
        time(&timer);
        struct tm* tm_info = localtime(&timer);
        char timebuf[26];
        strftime(timebuf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

        /* print a message */
        char application_message[256];
        snprintf(application_message, sizeof(application_message), "The time is %s", timebuf);

        if (publisher.Publish(topic, application_message))
            printf("%s published : \"%s\"", argv[0], application_message);
        else
        {
            std::cerr << "Error:" << publisher.GetError() << std::endl;
            break;
        }
    }

    /* disconnect */
    printf("\n%s disconnecting from %s\n", argv[0], addr);
}

void publish_callback(void** unused, struct mqtt_response_publish *published) 
{
    /* not used in this example */
}