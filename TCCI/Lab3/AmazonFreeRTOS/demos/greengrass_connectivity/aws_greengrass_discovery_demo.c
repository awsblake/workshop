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
 * @file aws_greengrass_discovery_demo.c
 * @brief A simple Greengrass discovery example.
 *
 * A simple example that perform discovery of the greengrass core device.
 * The JSON file is retrieved.
 */


/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "platform/iot_network.h"

/* Greengrass includes. */
#include "aws_ggd_config.h"
#include "aws_ggd_config_defaults.h"
#include "aws_greengrass_discovery.h"

/* MQTT includes. */
#include "aws_mqtt_agent.h"

/* Demo includes. */
#include "aws_demo_config.h"

/* Includes for initialization. */
#include "iot_mqtt.h"

#include "driver/gpio.h"
#include "driver/DHT22.h"

#include "freertos/queue.h"

/* JSON utilities include. */
#include "iot_json_utils.h"

#define ggdDEMO_MAX_MQTT_MESSAGES      3
#define ggdDEMO_MAX_MQTT_MSG_SIZE      500
#define ggdDEMO_DISCOVERY_FILE_SIZE    2500
#define ggdDEMO_MQTT_MSG_TOPIC         "freertos/demos/ggd"
#define ggdDEMO_MQTT_SUB_TOPIC         "freertos/demos/led"
#define ggdDEMO_MQTT_MSG_TEMPERATURE                               \
                                       "{"                         \
                                       "\"Humidity\":%.1f,"        \
                                       "\"Temperature\":%.1f"       \
                                       "}"

#define ggdDEMO_MQTT_MSG_VIBRATE                                   \
                                       "{"                         \
                                       "\"Detect\":%s"             \
                                       "}"
#define SUBSCRIBE_TOKEN_KEY            "led"
#define SUBSCRIBE_TOKEN_KEY_LENGTH     ( sizeof( SUBSCRIBE_TOKEN_KEY ) - 1 )

static xQueueHandle xDemoQueue = NULL;

static const char pcGgdTimerName[] = "GgdDemoTimer";
TimerHandle_t xGgdRequestTimer = NULL;
BaseType_t xGgdTimerStarted = pdFALSE;

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

/**
 * @brief Contains the user data for callback processing.
 */
typedef struct
{
    const char * pcExpectedString;      /**< Informs the MQTT callback of the next expected string. */
    BaseType_t xCallbackStatus;         /**< Used to communicate the success or failure of the callback function.
                                         * xCallbackStatus is set to pdFALSE before the callback is executed, and is
                                         * set to pdPASS inside the callback only if the callback receives the expected
                                         * data. */
    SemaphoreHandle_t xWakeUpSemaphore; /**< Handle of semaphore to wake up the task. */
    char * pcTopic;                     /**< Topic to subscribe and publish to. */
} GGDUserData_t;

/* The maximum time to wait for an MQTT operation to complete.  Needs to be
 * long enough for the TLS negotiation to complete. */
static const TickType_t xMaxCommandTime = pdMS_TO_TICKS( 20000UL );
static const TickType_t xTimeBetweenPublish = pdMS_TO_TICKS( 1500UL );
static char pcJSONFile[ ggdDEMO_DISCOVERY_FILE_SIZE ];

/*
 * The MQTT client used for all the publish and subscribes.
 */
static MQTTAgentHandle_t xMQTTClientHandle;
static BaseType_t prvMQTTConnect( GGD_HostAddressData_t * pxHostAddressData );
static void prvSendMessageToGGC( GGD_HostAddressData_t * pxHostAddressData );
static void prvDiscoverGreenGrassCore( void * pvParameters );


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

static MQTTBool_t prvMQTTCallback( void * pvUserData,
                                   const MQTTPublishData_t * const pxPublishParameters )
{
    bool keyFound = false;
    const char * pJsonValue = NULL;
    size_t jsonValueLength = 0;

    /* Print information about the incoming PUBLISH message. */
    configPRINTF(( "Incoming PUBLISH received:\r\n"
                "Publish payload: %.*s\r\n",
                pxPublishParameters->ulDataLength,
                pxPublishParameters->pvData ));

    /* Find the given section in the updated document. */
    keyFound = IotJsonUtils_FindJsonValue( pxPublishParameters->pvData,
                                               pxPublishParameters->ulDataLength,
                                               SUBSCRIBE_TOKEN_KEY,
                                               SUBSCRIBE_TOKEN_KEY_LENGTH,
                                               &pJsonValue,
                                               &jsonValueLength );

    if( keyFound == true )
    {
        configPRINTF(( "Turn Led %.*s\r\n",
                    jsonValueLength,
                    pJsonValue ));
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
        configPRINTF(( "Failed to find key %s in Json document.",
                      "led" ));
    }

    return eMQTTFalse;
}

/*-----------------------------------------------------------*/

static void prvSendMessageToGGC( GGD_HostAddressData_t * pxHostAddressData )
{
    const char * pcTopic = ggdDEMO_MQTT_MSG_TOPIC;
    MQTTAgentSubscribeParams_t xSubscribeParams;
    MQTTAgentPublishParams_t xPublishParams;
    MQTTAgentReturnCode_t xReturnCode;
    uint32_t ulMessageCounter;
    char cBuffer[ ggdDEMO_MAX_MQTT_MSG_SIZE ];

    DemoTaskMessage_t xMessage;

    if( prvMQTTConnect( pxHostAddressData ) == pdPASS )
    {
        /* Setup subscribe parameters to subscribe to echo topic. */
        xSubscribeParams.pucTopic = ( const uint8_t * )ggdDEMO_MQTT_SUB_TOPIC;
        xSubscribeParams.pxPublishCallback = prvMQTTCallback;
        xSubscribeParams.usTopicLength = ( uint16_t ) strlen( ( const char * ) ggdDEMO_MQTT_SUB_TOPIC );
        xSubscribeParams.xQoS = eMQTTQoS1;

        /* Subscribe to the topic. */
        xReturnCode = MQTT_AGENT_Subscribe( xMQTTClientHandle,
                                          &xSubscribeParams,
                                          xMaxCommandTime );
        TEST_ASSERT_EQUAL_INT( xReturnCode, eMQTTAgentSuccess );

        /* Publish to the topic to which this task is subscribed in order
         * to receive back the data that was published. */
        xPublishParams.xQoS = eMQTTQoS0;
        xPublishParams.pucTopic = ( const uint8_t * ) pcTopic;
        xPublishParams.usTopicLength = ( uint16_t ) ( strlen( pcTopic ) );

        if( xGgdRequestTimer != NULL )
        {
            xGgdTimerStarted = xTimerStart( xGgdRequestTimer, 0 );
        }

        if( xGgdTimerStarted == pdTRUE )
        {
            configPRINTF(( "Starting %s timer.\r\n", pcGgdTimerName ));
        }
        else
        {
            configPRINTF(( "ERROR: failed to start %s timer.\r\n", pcGgdTimerName ));
        }

        for( ulMessageCounter = 0;; ulMessageCounter++ )
        {
            if(pdTRUE == xQueueReceive(xDemoQueue, &xMessage, portMAX_DELAY))
            {
                /* Generate the payload for the PUBLISH. */
                if (xMessage.type == eEventTypeGpio)
                {
                    xPublishParams.ulDataLength = sprintf( cBuffer,
                                    ggdDEMO_MQTT_MSG_VIBRATE,
                                    "\"Vibrating\"" );
                }
                else if(xMessage.type == eEventTypeTemp)
                {
                    xPublishParams.ulDataLength = sprintf( cBuffer,
                                    ggdDEMO_MQTT_MSG_TEMPERATURE,
                                    xMessage.humidity, xMessage.temperature );
                }
                else
                {
                    /* Generate the payload for the PUBLISH. */
                    xPublishParams.ulDataLength = sprintf( cBuffer,
                                    "%s",
                                    "Failed to get event type." );
                }
                xPublishParams.pvData = cBuffer;
                xReturnCode = MQTT_AGENT_Publish( xMQTTClientHandle,
                                                &xPublishParams,
                                                xMaxCommandTime );

                if( xReturnCode != eMQTTAgentSuccess )
                {
                    configPRINTF( ( "mqtt_client - Failure to publish \n" ) );
                }

                vTaskDelay( xTimeBetweenPublish );
            }
        }

        configPRINTF( ( "Disconnecting from broker.\r\n" ) );

        if( MQTT_AGENT_Disconnect( xMQTTClientHandle,
                                   xMaxCommandTime ) == eMQTTAgentSuccess )
        {
            configPRINTF( ( "Disconnected from the broker.\r\n" ) );

            if( MQTT_AGENT_Delete( xMQTTClientHandle ) == eMQTTAgentSuccess )
            {
                configPRINTF( ( "Deleted Client.\r\n" ) );
            }
            else
            {
                configPRINTF( ( "ERROR:  MQTT_AGENT_Delete() Failed.\r\n" ) );
            }
        }
        else
        {
            configPRINTF( ( "ERROR:  Did not disconnected from the broker.\r\n" ) );
        }
    }
}

/*-----------------------------------------------------------*/

static BaseType_t prvMQTTConnect( GGD_HostAddressData_t * pxHostAddressData )
{
    MQTTAgentConnectParams_t xConnectParams;
    BaseType_t xResult = pdPASS;

    /* Connect to the broker. */
    xConnectParams.pucClientId = ( const uint8_t * ) ( clientcredentialIOT_THING_NAME );
    xConnectParams.usClientIdLength = ( uint16_t ) ( strlen( clientcredentialIOT_THING_NAME ) );
    xConnectParams.pcURL = pxHostAddressData->pcHostAddress;
    xConnectParams.usPort = clientcredentialMQTT_BROKER_PORT;
    xConnectParams.xFlags = mqttagentREQUIRE_TLS | mqttagentURL_IS_IP_ADDRESS;
    xConnectParams.xURLIsIPAddress = pdTRUE; /* Deprecated. */
    xConnectParams.pcCertificate = pxHostAddressData->pcCertificate;
    xConnectParams.ulCertificateSize = pxHostAddressData->ulCertificateSize;
    xConnectParams.pvUserData = NULL;
    xConnectParams.pxCallback = NULL;
    xConnectParams.xSecuredConnection = pdTRUE; /* Deprecated. */

    if( MQTT_AGENT_Connect( xMQTTClientHandle,
                            &xConnectParams,
                            xMaxCommandTime ) != eMQTTAgentSuccess )
    {
        configPRINTF( ( "ERROR: Could not connect to the Broker.\r\n" ) );
        xResult = pdFAIL;
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static void prvDiscoverGreenGrassCore( void * pvParameters )
{
    GGD_HostAddressData_t xHostAddressData;

    ( void ) pvParameters;

    /* Create MQTT Client. */
    if( MQTT_AGENT_Create( &( xMQTTClientHandle ) ) == eMQTTAgentSuccess )
    {
        memset( &xHostAddressData, 0, sizeof( xHostAddressData ) );

        /* Demonstrate automated connection. */
        configPRINTF( ( "Attempting automated selection of Greengrass device\r\n" ) );

        if( GGD_GetGGCIPandCertificate( pcJSONFile,
                                        ggdDEMO_DISCOVERY_FILE_SIZE,
                                        &xHostAddressData )
            == pdPASS )
        {
            configPRINTF( ( "Greengrass device discovered.\r\n" ) );
            configPRINTF( ( "Establishing MQTT communication to Greengrass...\r\n" ) );
            prvSendMessageToGGC( &xHostAddressData );

            /* Report on space efficiency of this demo task. */
            #if ( INCLUDE_uxTaskGetStackHighWaterMark == 1 )
                {
                    configPRINTF( ( "Heap low watermark: %u. Stack high watermark: %u.\r\n",
                                    xPortGetMinimumEverFreeHeapSize(),
                                    uxTaskGetStackHighWaterMark( NULL ) ) );
                }
            #endif
        }
        else
        {
            configPRINTF( ( "Auto-connect: Failed to retrieve Greengrass address and certificate.\r\n" ) );
        }
    }

    configPRINTF( ( "----Demo finished----\r\n" ) );
    vTaskDelete( NULL );
}

/*-----------------------------------------------------------*/

int vStartGreenGrassDiscoveryTask( bool awsIotMqttMode,
                 const char * pIdentifier,
                 void * pNetworkServerInfo,
                 void * pNetworkCredentialInfo,
                 const IotNetworkInterface_t * pNetworkInterface )
{
	/* Unused parameters */
	( void )awsIotMqttMode;
	( void )pIdentifier;
	( void )pNetworkServerInfo;
	( void )pNetworkCredentialInfo;
	( void )pNetworkInterface;

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

    if( xGgdRequestTimer == NULL )
    {
        xGgdRequestTimer = xTimerCreate( pcGgdTimerName,
                                      pdMS_TO_TICKS( 3000 ),
                                      pdTRUE,
                                      NULL,
                                      prvRequestTimer_Callback );
    }

    prvDiscoverGreenGrassCore( NULL );
    return 0;
}
