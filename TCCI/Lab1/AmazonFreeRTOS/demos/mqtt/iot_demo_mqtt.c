/*
 * Amazon FreeRTOS V201906.00 Major
 * Copyright (C) 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file iot_demo_mqtt.c
 * @brief Demonstrates usage of the MQTT library.
 */

/* The config header is always included first. */
#include "iot_config.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* MQTT include. */
#include "iot_mqtt.h"

#include "driver/gpio.h"
#include "driver/DHT22.h"

#include "freertos/queue.h"

/* JSON utilities include. */
#include "iot_json_utils.h"

/**
 * @cond DOXYGEN_IGNORE
 * Doxygen should ignore this section.
 *
 * Provide default values for undefined configuration settings.
 */
#ifndef IOT_DEMO_MQTT_TOPIC_PREFIX
    #define IOT_DEMO_MQTT_TOPIC_PREFIX           "iotdemo"
#endif
#ifndef IOT_DEMO_MQTT_PUBLISH_BURST_SIZE
    #define IOT_DEMO_MQTT_PUBLISH_BURST_SIZE     ( 10 )
#endif
#ifndef IOT_DEMO_MQTT_PUBLISH_BURST_COUNT
    #define IOT_DEMO_MQTT_PUBLISH_BURST_COUNT    ( 10 )
#endif
/** @endcond */

/* Validate MQTT demo configuration settings. */
#if IOT_DEMO_MQTT_PUBLISH_BURST_SIZE <= 0
    #error "IOT_DEMO_MQTT_PUBLISH_BURST_SIZE cannot be 0 or negative."
#endif
#if IOT_DEMO_MQTT_PUBLISH_BURST_COUNT <= 0
    #error "IOT_DEMO_MQTT_PUBLISH_BURST_COUNT cannot be 0 or negative."
#endif

/**
 * @brief The first characters in the client identifier. A timestamp is appended
 * to this prefix to create a unique client identifer.
 *
 * This prefix is also used to generate topic names and topic filters used in this
 * demo.
 */
#define CLIENT_IDENTIFIER_PREFIX                 "iotdemo"

/**
 * @brief The longest client identifier that an MQTT server must accept (as defined
 * by the MQTT 3.1.1 spec) is 23 characters. Add 1 to include the length of the NULL
 * terminator.
 */
#define CLIENT_IDENTIFIER_MAX_LENGTH             ( 24 )

/**
 * @brief The keep-alive interval used for this demo.
 *
 * An MQTT ping request will be sent periodically at this interval.
 */
#define KEEP_ALIVE_SECONDS                       ( 60 )

/**
 * @brief The timeout for MQTT operations in this demo.
 */
#define MQTT_TIMEOUT_MS                          ( 5000 )

/**
 * @brief The Last Will and Testament topic name in this demo.
 *
 * The MQTT server will publish a message to this topic name if this client is
 * unexpectedly disconnected.
 */
#define WILL_TOPIC_NAME                          IOT_DEMO_MQTT_TOPIC_PREFIX "/will"

/**
 * @brief The length of #WILL_TOPIC_NAME.
 */
#define WILL_TOPIC_NAME_LENGTH                   ( ( uint16_t ) ( sizeof( WILL_TOPIC_NAME ) - 1 ) )

/**
 * @brief The message to publish to #WILL_TOPIC_NAME.
 */
#define WILL_MESSAGE                             "MQTT demo unexpectedly disconnected."

/**
 * @brief The length of #WILL_MESSAGE.
 */
#define WILL_MESSAGE_LENGTH                      ( ( size_t ) ( sizeof( WILL_MESSAGE ) - 1 ) )

/**
 * @brief How many topic filters will be used in this demo.
 */
#define TOPIC_FILTER_COUNT                       ( 1 )

/**
 * @brief The length of each topic filter.
 *
 * For convenience, all topic filters are the same length.
 */
#define TOPIC_FILTER_LENGTH                      ( ( uint16_t ) ( sizeof( IOT_DEMO_MQTT_TOPIC_PREFIX "/topic/XXX" ) - 1 ) )

/**
 * @brief Format string of the PUBLISH messages in this demo.
 */
#define PUBLISH_DHT_PAYLOAD_FORMAT                       \
                             "{"                         \
                             "\"Humidity\":%.1f,"        \
                             "\"Temperature\":%.1f"       \
                             "}"

#define PUBLISH_VIB_PAYLOAD_FORMAT                       \
                             "{"                         \
                             "\"Detect\":%s"             \
                             "}"

/**
 * @brief Size of the buffer that holds the PUBLISH messages in this demo.
 */
#define PUBLISH_PAYLOAD_BUFFER_LENGTH            ( sizeof( PUBLISH_DHT_PAYLOAD_FORMAT ) + 2 )

/**
 * @brief The maximum number of times each PUBLISH in this demo will be retried.
 */
#define PUBLISH_RETRY_LIMIT                      ( 10 )

/**
 * @brief A PUBLISH message is retried if no response is received within this
 * time.
 */
#define PUBLISH_RETRY_MS                         ( 1000 )


/**
 * @brief The JSON key used to represent tokens in a SUBSCRIBE message.
 */
#define SUBSCRIBE_TOKEN_KEY                      "led"

/**
 * @brief The length of #SUBSCRIBE_TOKEN_KEY.
 */
#define SUBSCRIBE_TOKEN_KEY_LENGTH               ( sizeof( SUBSCRIBE_TOKEN_KEY ) - 1 )

/*-----------------------------------------------------------*/

static xQueueHandle xDemoQueue = NULL;

static const char pcTimerName[] = "DemoTimer";
TimerHandle_t xRequestTimer = NULL;
BaseType_t xTimerStarted = pdFALSE;

typedef enum
{
    eEventTypeNone,
    eEventTypeGpio,
    eEventTypeTemp,
} DemoEventType_t;

typedef struct DemoTaskMessage
{
    DemoEventType_t type;
    float humidity;
    float temperature;
} DemoTaskMessage_t;


/*-----------------------------------------------------------*/

/* Declaration of demo function. */
int RunMqttDemo( bool awsIotMqttMode,
                 const char * pIdentifier,
                 void * pNetworkServerInfo,
                 void * pNetworkCredentialInfo,
                 const IotNetworkInterface_t * pNetworkInterface );

/*-----------------------------------------------------------*/

/**
 * @brief Called by the MQTT library when an operation completes.
 *
 * The demo uses this callback to determine the result of PUBLISH operations.
 * @param[in] param1 The number of the PUBLISH that completed, passed as an intptr_t.
 * @param[in] pOperation Information about the completed operation passed by the
 * MQTT library.
 */
static void _operationCompleteCallback( void * param1,
                                        IotMqttCallbackParam_t * const pOperation )
{
    intptr_t publishCount = ( intptr_t ) param1;

    /* Silence warnings about unused variables. publishCount will not be used if
     * logging is disabled. */
    ( void ) publishCount;

    /* Print the status of the completed operation. A PUBLISH operation is
     * successful when transmitted over the network. */
    if( pOperation->u.operation.result == IOT_MQTT_SUCCESS )
    {
        IotLogInfo( "MQTT %s %d successfully sent.",
                    IotMqtt_OperationType( pOperation->u.operation.type ),
                    ( int ) publishCount );
    }
    else
    {
        IotLogError( "MQTT %s %d could not be sent. Error %s.",
                     IotMqtt_OperationType( pOperation->u.operation.type ),
                     ( int ) publishCount,
                     IotMqtt_strerror( pOperation->u.operation.result ) );
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Called by the MQTT library when an incoming PUBLISH message is received.
 *
 * The demo uses this callback to handle incoming PUBLISH messages. This callback
 * prints the contents of an incoming message and publishes an acknowledgement
 * to the MQTT server.
 * @param[in] param1 Counts the total number of received PUBLISH messages. This
 * callback will increment this counter.
 * @param[in] pPublish Information about the incoming PUBLISH message passed by
 * the MQTT library.
 */
static void _mqttSubscriptionCallback( void * param1,
                                       IotMqttCallbackParam_t * const pPublish )
{
    IotSemaphore_t * pPublishesReceived = ( IotSemaphore_t * ) param1;
    const char * pPayload = pPublish->u.message.info.pPayload;
    bool keyFound = false;
    const char * pJsonValue = NULL;
    size_t jsonValueLength = 0;

    /* Print information about the incoming PUBLISH message. */
    IotLogInfo( "Incoming PUBLISH received:\r\n"
                "Subscription topic filter: %.*s\r\n"
                "Publish topic name: %.*s\r\n"
                "Publish retain flag: %d\r\n"
                "Publish QoS: %d\r\n"
                "Publish payload: %.*s",
                pPublish->u.message.topicFilterLength,
                pPublish->u.message.pTopicFilter,
                pPublish->u.message.info.topicNameLength,
                pPublish->u.message.info.pTopicName,
                pPublish->u.message.info.retain,
                pPublish->u.message.info.qos,
                pPublish->u.message.info.payloadLength,
                pPayload );


    /* Find the given section in the updated document. */
    keyFound = IotJsonUtils_FindJsonValue( pPublish->u.message.info.pPayload,
                                               pPublish->u.message.info.payloadLength,
                                               SUBSCRIBE_TOKEN_KEY,
                                               SUBSCRIBE_TOKEN_KEY_LENGTH,
                                               &pJsonValue,
                                               &jsonValueLength );

    if( keyFound == true )
    {
        IotLogInfo( "Turn Led %.*s\r\n",
                    jsonValueLength,
                    pJsonValue );
        if ( strncmp( pJsonValue, "\"on\"", 4 ) == 0 )
        {
            gpio_set_level(GPIO_NUM_13, 0);
        }
        else
        {
            gpio_set_level(GPIO_NUM_13, 1);
        }
    }
    else
    {
        IotLogWarn( "Failed to find key %s in Json document.",
                    "led" );
    }

    /* Increment the number of PUBLISH messages received. */
    IotSemaphore_Post( pPublishesReceived );
}

/*-----------------------------------------------------------*/

/**
 * @brief Initialize the MQTT library.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
static int _initializeDemo( void )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t mqttInitStatus = IOT_MQTT_SUCCESS;

    mqttInitStatus = IotMqtt_Init();

    if( mqttInitStatus != IOT_MQTT_SUCCESS )
    {
        /* Failed to initialize MQTT library. */
        status = EXIT_FAILURE;
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief Clean up the MQTT library.
 */
static void _cleanupDemo( void )
{
    IotMqtt_Cleanup();
}

/*-----------------------------------------------------------*/

/**
 * @brief Establish a new connection to the MQTT server.
 *
 * @param[in] awsIotMqttMode Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pIdentifier NULL-terminated MQTT client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[out] pMqttConnection Set to the handle to the new MQTT connection.
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
static int _establishMqttConnection( bool awsIotMqttMode,
                                     const char * pIdentifier,
                                     void * pNetworkServerInfo,
                                     void * pNetworkCredentialInfo,
                                     const IotNetworkInterface_t * pNetworkInterface,
                                     IotMqttConnection_t * pMqttConnection )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t connectStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttNetworkInfo_t networkInfo = IOT_MQTT_NETWORK_INFO_INITIALIZER;
    IotMqttConnectInfo_t connectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;
    IotMqttPublishInfo_t willInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    char pClientIdentifierBuffer[ CLIENT_IDENTIFIER_MAX_LENGTH ] = { 0 };

    /* Set the members of the network info not set by the initializer. This
     * struct provided information on the transport layer to the MQTT connection. */
    networkInfo.createNetworkConnection = true;
    networkInfo.u.setup.pNetworkServerInfo = pNetworkServerInfo;
    networkInfo.u.setup.pNetworkCredentialInfo = pNetworkCredentialInfo;
    networkInfo.pNetworkInterface = pNetworkInterface;

    #if ( IOT_MQTT_ENABLE_SERIALIZER_OVERRIDES == 1 ) && defined( IOT_DEMO_MQTT_SERIALIZER )
        networkInfo.pMqttSerializer = IOT_DEMO_MQTT_SERIALIZER;
    #endif

    /* Set the members of the connection info not set by the initializer. */
    connectInfo.awsIotMqttMode = awsIotMqttMode;
    connectInfo.cleanSession = true;
    connectInfo.keepAliveSeconds = KEEP_ALIVE_SECONDS;
    connectInfo.pWillInfo = &willInfo;

    /* Set the members of the Last Will and Testament (LWT) message info. The
     * MQTT server will publish the LWT message if this client disconnects
     * unexpectedly. */
    willInfo.pTopicName = WILL_TOPIC_NAME;
    willInfo.topicNameLength = WILL_TOPIC_NAME_LENGTH;
    willInfo.pPayload = WILL_MESSAGE;
    willInfo.payloadLength = WILL_MESSAGE_LENGTH;

    /* Use the parameter client identifier if provided. Otherwise, generate a
     * unique client identifier. */
    if( pIdentifier != NULL )
    {
        connectInfo.pClientIdentifier = pIdentifier;
        connectInfo.clientIdentifierLength = ( uint16_t ) strlen( pIdentifier );
    }
    else
    {
        /* Every active MQTT connection must have a unique client identifier. The demos
         * generate this unique client identifier by appending a timestamp to a common
         * prefix. */
        status = snprintf( pClientIdentifierBuffer,
                           CLIENT_IDENTIFIER_MAX_LENGTH,
                           CLIENT_IDENTIFIER_PREFIX "%lu",
                           ( long unsigned int ) IotClock_GetTimeMs() );

        /* Check for errors from snprintf. */
        if( status < 0 )
        {
            IotLogError( "Failed to generate unique client identifier for demo." );
            status = EXIT_FAILURE;
        }
        else
        {
            /* Set the client identifier buffer and length. */
            connectInfo.pClientIdentifier = pClientIdentifierBuffer;
            connectInfo.clientIdentifierLength = ( uint16_t ) status;

            status = EXIT_SUCCESS;
        }
    }

    /* Establish the MQTT connection. */
    if( status == EXIT_SUCCESS )
    {
        IotLogInfo( "MQTT demo client identifier is %.*s (length %hu).",
                    connectInfo.clientIdentifierLength,
                    connectInfo.pClientIdentifier,
                    connectInfo.clientIdentifierLength );

        connectStatus = IotMqtt_Connect( &networkInfo,
                                         &connectInfo,
                                         MQTT_TIMEOUT_MS,
                                         pMqttConnection );

        if( connectStatus != IOT_MQTT_SUCCESS )
        {
            IotLogError( "MQTT CONNECT returned error %s.",
                         IotMqtt_strerror( connectStatus ) );

            status = EXIT_FAILURE;
        }
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief Add or remove subscriptions by either subscribing or unsubscribing.
 *
 * @param[in] mqttConnection The MQTT connection to use for subscriptions.
 * @param[in] operation Either #IOT_MQTT_SUBSCRIBE or #IOT_MQTT_UNSUBSCRIBE.
 * @param[in] pTopicFilters Array of topic filters for subscriptions.
 * @param[in] pCallbackParameter The parameter to pass to the subscription
 * callback.
 *
 * @return `EXIT_SUCCESS` if the subscription operation succeeded; `EXIT_FAILURE`
 * otherwise.
 */
static int _modifySubscriptions( IotMqttConnection_t mqttConnection,
                                 IotMqttOperationType_t operation,
                                 const char * pTopicFilters,
                                 void * pCallbackParameter )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t subscriptionStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttSubscription_t subscriptions = IOT_MQTT_SUBSCRIPTION_INITIALIZER;

    /* Set the members of the subscription list. */
    subscriptions.qos = IOT_MQTT_QOS_1;
    subscriptions.pTopicFilter = pTopicFilters;
    subscriptions.topicFilterLength = TOPIC_FILTER_LENGTH;
    subscriptions.callback.pCallbackContext = pCallbackParameter;
    subscriptions.callback.function = _mqttSubscriptionCallback;

    /* Modify subscriptions by either subscribing or unsubscribing. */
    if( operation == IOT_MQTT_SUBSCRIBE )
    {
        subscriptionStatus = IotMqtt_TimedSubscribe( mqttConnection,
                                                     &subscriptions,
                                                     TOPIC_FILTER_COUNT,
                                                     0,
                                                     MQTT_TIMEOUT_MS );

        /* Check the status of SUBSCRIBE. */
        switch( subscriptionStatus )
        {
            case IOT_MQTT_SUCCESS:
                IotLogInfo( "All demo topic filter subscriptions accepted." );
                break;

            case IOT_MQTT_SERVER_REFUSED:

                /* Check which subscriptions were rejected before exiting the demo. */
                if( IotMqtt_IsSubscribed( mqttConnection,
                                          subscriptions.pTopicFilter,
                                          subscriptions.topicFilterLength,
                                          NULL ) == true )
                {
                    IotLogInfo( "Topic filter %.*s was accepted.",
                                subscriptions.topicFilterLength,
                                subscriptions.pTopicFilter );
                }
                else
                {
                    IotLogError( "Topic filter %.*s was rejected.",
                                 subscriptions.topicFilterLength,
                                 subscriptions.pTopicFilter );
                }

                status = EXIT_FAILURE;
                break;

            default:

                status = EXIT_FAILURE;
                break;
        }
    }
    else if( operation == IOT_MQTT_UNSUBSCRIBE )
    {
        subscriptionStatus = IotMqtt_TimedUnsubscribe( mqttConnection,
                                                       &subscriptions,
                                                       TOPIC_FILTER_COUNT,
                                                       0,
                                                       MQTT_TIMEOUT_MS );

        /* Check the status of UNSUBSCRIBE. */
        if( subscriptionStatus != IOT_MQTT_SUCCESS )
        {
            status = EXIT_FAILURE;
        }
    }
    else
    {
        /* Only SUBSCRIBE and UNSUBSCRIBE are valid for modifying subscriptions. */
        IotLogError( "MQTT operation %s is not valid for modifying subscriptions.",
                     IotMqtt_OperationType( operation ) );

        status = EXIT_FAILURE;
    }

    return status;
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    DemoTaskMessage_t xMessage;
    gpio_set_level(GPIO_NUM_13, 0);

    xMessage.type = eEventTypeGpio;
    xQueueSendFromISR(xDemoQueue, &xMessage, pdFALSE );
}

static void prvRequestTimer_Callback( TimerHandle_t xTimer )
{
    int ret;
    DemoTaskMessage_t xMessage;

    ret = readDHT();
	errorHandler(ret);

    xMessage.type = eEventTypeTemp;
    xMessage.humidity = getHumidity();
    xMessage.temperature = getTemperature();

    printf( "Hum %.1f\n", xMessage.humidity );
    printf( "Tmp %.1f\n", xMessage.temperature );

    xQueueSend(xDemoQueue, &xMessage, ( TickType_t ) 0 );
}

/*-----------------------------------------------------------*/

/**
 * @brief Transmit all messages and wait for them to be received on topic filters.
 *
 * @param[in] mqttConnection The MQTT connection to use for publishing.
 * @param[in] pTopicNames Array of topic names for publishing. These were previously
 * subscribed to as topic filters.
 * @param[in] pPublishReceivedCounter Counts the number of messages received on
 * topic filters.
 *
 * @return `EXIT_SUCCESS` if all messages are published and received; `EXIT_FAILURE`
 * otherwise.
 */
static int _publishAllMessages( IotMqttConnection_t mqttConnection,
                                const char * pTopicNames,
                                IotSemaphore_t * pPublishReceivedCounter )
{
    int status = EXIT_SUCCESS;
    intptr_t publishCount = 0, i = 0;
    IotMqttError_t publishStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttPublishInfo_t publishInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    IotMqttCallbackInfo_t publishComplete = IOT_MQTT_CALLBACK_INFO_INITIALIZER;
    char pPublishPayload[ PUBLISH_PAYLOAD_BUFFER_LENGTH ] = { 0 };

    DemoTaskMessage_t xMessage;

    /* The MQTT library should invoke this callback when a PUBLISH message
     * is successfully transmitted. */
    publishComplete.function = _operationCompleteCallback;

    /* Set the common members of the publish info. */
    publishInfo.qos = IOT_MQTT_QOS_1;
    publishInfo.topicNameLength = TOPIC_FILTER_LENGTH;
    publishInfo.pPayload = pPublishPayload;
    publishInfo.retryMs = PUBLISH_RETRY_MS;
    publishInfo.retryLimit = PUBLISH_RETRY_LIMIT;
    publishInfo.pTopicName = pTopicNames;

    if( xRequestTimer != NULL )
    {
        xTimerStarted = xTimerStart( xRequestTimer, 0 );
    }

    if( xTimerStarted == pdTRUE )
    {
        IotLogInfo( "Starting %s timer.\r\n", pcTimerName );
    }
    else
    {
        IotLogError( "ERROR: failed to start %s timer.\r\n", pcTimerName );
    }

    /* Loop to PUBLISH all messages of this demo. */
    for(;;publishCount++)
    {
        if(pdTRUE == xQueueReceive(xDemoQueue, &xMessage, portMAX_DELAY))
        {
            /* Pass the PUBLISH number to the operation complete callback. */
            publishComplete.pCallbackContext = ( void * ) publishCount;

            /* Generate the payload for the PUBLISH. */
            if (xMessage.type == eEventTypeGpio)
            {
                status = snprintf( pPublishPayload,
                                PUBLISH_PAYLOAD_BUFFER_LENGTH,
                                PUBLISH_VIB_PAYLOAD_FORMAT,
                                "\"Vibrating\"" );
            }
            else if(xMessage.type == eEventTypeTemp)
            {
                status = snprintf( pPublishPayload,
                                PUBLISH_PAYLOAD_BUFFER_LENGTH,
                                PUBLISH_DHT_PAYLOAD_FORMAT,
                                xMessage.humidity, xMessage.temperature );
            }
            else
            {
                /* Generate the payload for the PUBLISH. */
                status = snprintf( pPublishPayload,
                                PUBLISH_PAYLOAD_BUFFER_LENGTH,
                                "Failed to get event type." );
            }

            /* Check for errors from snprintf. */
            if( status < 0 )
            {
                IotLogError( "Failed to generate MQTT PUBLISH payload for PUBLISH %d.",
                            ( int ) publishCount );
                status = EXIT_FAILURE;

                break;
            }
            else
            {
                publishInfo.payloadLength = ( size_t ) status;
                status = EXIT_SUCCESS;
            }

            /* PUBLISH a message. This is an asynchronous function that notifies of
            * completion through a callback. */
            publishStatus = IotMqtt_Publish( mqttConnection,
                                            &publishInfo,
                                            0,
                                            &publishComplete,
                                            NULL );

            if( publishStatus != IOT_MQTT_STATUS_PENDING )
            {
                IotLogError( "MQTT PUBLISH %d returned error %s.",
                            ( int ) publishCount,
                            IotMqtt_strerror( publishStatus ) );
                status = EXIT_FAILURE;

                break;
            }

            /* Stop publishing if there was an error. */
            if( status == EXIT_FAILURE )
            {
                break;
            }
        }
    }

    /* Wait for the messages in the last burst to be received. This should also
     * wait for all previously published messages. */
    if( status == EXIT_SUCCESS )
    {
        IotLogInfo( "Waiting for all publishes to be received." );

        for( i = 0; i < IOT_DEMO_MQTT_PUBLISH_BURST_SIZE; i++ )
        {
            if( IotSemaphore_TimedWait( pPublishReceivedCounter,
                                        MQTT_TIMEOUT_MS ) == false )
            {
                IotLogError( "Timed out waiting for incoming PUBLISH messages." );
                status = EXIT_FAILURE;

                break;
            }
        }

        if( i == IOT_DEMO_MQTT_PUBLISH_BURST_SIZE )
        {
            IotLogInfo( "All publishes received." );
        }
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief The function that runs the MQTT demo, called by the demo runner.
 *
 * @param[in] awsIotMqttMode Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pIdentifier NULL-terminated MQTT client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return `EXIT_SUCCESS` if the demo completes successfully; `EXIT_FAILURE` otherwise.
 */
int RunMqttDemo( bool awsIotMqttMode,
                 const char * pIdentifier,
                 void * pNetworkServerInfo,
                 void * pNetworkCredentialInfo,
                 const IotNetworkInterface_t * pNetworkInterface )
{
    /* Return value of this function and the exit status of this program. */
    int status = EXIT_SUCCESS;

    /* Handle of the MQTT connection used in this demo. */
    IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;

    /* Counts the number of incoming PUBLISHES received (and allows the demo
     * application to wait on incoming PUBLISH messages). */
    IotSemaphore_t publishesReceived;

    const char * pPublishTopic = IOT_DEMO_MQTT_TOPIC_PREFIX "/topic/pub";

    const char * pSubscribeTopic = IOT_DEMO_MQTT_TOPIC_PREFIX "/topic/sub";

    /* Flags for tracking which cleanup functions must be called. */
    bool librariesInitialized = false, connectionEstablished = false;

    /* Initialize the libraries required for this demo. */
    status = _initializeDemo();

    gpio_config_t gpio14_conf = {
        .pin_bit_mask = GPIO_SEL_14,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio14_conf);
    gpio_set_intr_type(GPIO_NUM_14, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_14, gpio_isr_handler, (void*) GPIO_NUM_14);

	setDHTgpio(25);

    gpio_config_t gpio13_conf = {
        .pin_bit_mask = GPIO_SEL_13,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio13_conf);
    gpio_set_level(GPIO_NUM_13, 1);

    xDemoQueue = xQueueCreate(10, sizeof(DemoTaskMessage_t));

    if( xRequestTimer == NULL )
    {
        xRequestTimer = xTimerCreate( pcTimerName,
                                      pdMS_TO_TICKS( 3000 ),
                                      pdTRUE,
                                      NULL,
                                      prvRequestTimer_Callback );
    }

    if( status == EXIT_SUCCESS )
    {
        /* Mark the libraries as initialized. */
        librariesInitialized = true;

        /* Establish a new MQTT connection. */
        status = _establishMqttConnection( awsIotMqttMode,
                                           pIdentifier,
                                           pNetworkServerInfo,
                                           pNetworkCredentialInfo,
                                           pNetworkInterface,
                                           &mqttConnection );
    }

    if( status == EXIT_SUCCESS )
    {
        /* Mark the MQTT connection as established. */
        connectionEstablished = true;

        /* Add the topic filter subscriptions used in this demo. */
        status = _modifySubscriptions( mqttConnection,
                                       IOT_MQTT_SUBSCRIBE,
                                       pSubscribeTopic,
                                       &publishesReceived );
    }

    if( status == EXIT_SUCCESS )
    {
        /* Create the semaphore to count incoming PUBLISH messages. */
        if( IotSemaphore_Create( &publishesReceived,
                                 0,
                                 IOT_DEMO_MQTT_PUBLISH_BURST_SIZE ) == true )
        {
            /* PUBLISH (and wait) for all messages. */
            status = _publishAllMessages( mqttConnection,
                                          pPublishTopic,
                                          &publishesReceived );

            /* Destroy the incoming PUBLISH counter. */
            IotSemaphore_Destroy( &publishesReceived );
        }
        else
        {
            /* Failed to create incoming PUBLISH counter. */
            status = EXIT_FAILURE;
        }
    }

    if( status == EXIT_SUCCESS )
    {
        /* Remove the topic subscription filters used in this demo. */
        status = _modifySubscriptions( mqttConnection,
                                       IOT_MQTT_UNSUBSCRIBE,
                                       pSubscribeTopic,
                                       NULL );
    }

    /* Disconnect the MQTT connection if it was established. */
    if( connectionEstablished == true )
    {
        IotMqtt_Disconnect( mqttConnection, 0 );
    }

    /* Clean up libraries if they were initialized. */
    if( librariesInitialized == true )
    {
        _cleanupDemo();
    }

    return status;
}

/*-----------------------------------------------------------*/
