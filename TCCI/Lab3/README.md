# TCCI Workshop Lab3

Complete your Greengrass group configuration by setting up a Lambda function, client device and some subscription rules. You will also build a test client and obtain the required certificates to connect a Greengrass aware device.

## Architecture

[Image: image.png]
## Step 1 - Creating a Lambda function for the core

There are a few things we need to setup before we configure our group and that is a test lambda function.
This function will wait to be invoked and when it is, it will publish a message to a cloud topic. The workflow here is as follows:
***Test Device -> Greengrass Core -> Lambda -> AWS IoT (Via Core)***
The test device will publish on a top called** freertos/demos/ggd **and that will route to the local lambda function which in turn publishes a response message to the cloud on topic **iotdemo/topic/pub**. When using the MQTT Client in the console we will see this publish-response.
Lambda functions require some additional modules that aren’t intrinsic to Lambda and need to be packaged and uploaded.

Next we need to create our new Lambda and upload the zipped TcciWorkshop_Hello lambda function.

* Open the Lambda console
* Click **Create function**
* Select the** Author from scratch**
* For the Name enter **Greengrass_HelloWorld**
* Select** Python 2.7** for the runtime
* For **Permissions**, keep the default setting
* Click Create function

* [Image: image.png]
* Upload your Lambda function deployment package:
    * On the **Configuration** tab, under **Function code**, set the following fields:

        * For **Code entry type**, choose **Upload a .zip file**.
        * For **Runtime**, choose **Python 2.7**.
        * For **Handler**, enter **greengrassHelloWorld.function_handler**
    * Choose **Upload**, and then choose **hello_world_python_lambda.zip**.

[Image: image.png]
* Choose Save

[Image: image.png]
## Step 2 - Attach our lambda function to the group

Before we can attach the lambda we need to publish it and use the alias in the group configuration.

* From **Actions**, choose **Publish new version**.

[Image: image.png]
* For **Version description**, enter **First version**, and then choose **Publish**.

[Image: image.png]
* Create an alias for the Lambda function version

[Image: image.png]
* Name the alias **GG_HelloWorld**, set the version to **1** (which corresponds to the version that you just published), and then choose **Create**.

[Image: image.png]Ok, we now have an alias. When we make changes later we only have to update our alias’s version and then do a group deployment to send new code to our core.
Now we need to attach this lambda to our Greengrass group.

* Open the Greengrass console and select the TcciWorkshop group
* Click on Lambdas and select Add your first lambda

[Image: image.png]
* Choose **Use existing Lambda**

[Image: image.png]
* Search for the name of the Lambda you created in the previous step (**Greengrass_HelloWorld**, not the alias name), select it, and then choose **Next**

[Image: image.png]
* For the version, choose **Alias: GG_HelloWorld**, and then choose **Finish**. You should see the **Greengrass_HelloWorld** Lambda function in your group, using the **GG_HelloWorld** alias
* Choose the ellipsis (**…**), and then choose **Edit Configuration**

[Image: image.png]
* On the **Group-specific Lambda configuration** page, make the following changes:

    * Set **Timeout** to 25 seconds. This Lambda function sleeps for 20 seconds before each invocation.
    * For **Lambda lifecycle**, choose **Make this function long-lived and keep it running indefinitely**.
    * Click **Update**

[Image: image.png]Congrats, your lambda is now part of the group. You can also attach different versions and aliases to the same group at the same time!

## Step 3 - Create a test thing/device

Before we can configure any routing rules (Subscriptions) we need to create a test device or thing and it’s required certificates.
Note that Greengrass devices are just regular “things” - if you have the certificates of existing things you can use those.

* On your group configuration page click on Devices
* Click Add your first device

[Image: image.png]
* Click on Select an IoT Thing

[Image: image.png]
* Select **tcci-workshop** and click** Finish**

[Image: image.png]
## Step 4 - Create our Subscriptions

All data flow within Greengrass must be explicitly defined. These routing rules are called subscriptions.
For this lab we need to create three subscriptions. We need to allow data from our ESP32 to trigger the lambda function and then we need to allow the lambda function to publish messages to our cloud topic and ESP32.

* On your group configuration page click on Subscriptions
* Click on Add your first Subscription

[Image: image.png]
* The source should be our Device - tcci-workshop
* The target should be our Lambda - Greengrass_HelloWorld

[Image: image.png]
* Click Next
* For the topic filter enter freertos/demos/ggd

[Image: image.png]
* Click Next and then Finish

Let’s create another subscription, this next one allows traffic from the Lambda to the cloud.

* Click Add Subscription
* The source should be our Lambda - GreengrassHelloWorld
* The target should be the IoT Cloud
* For the topic filter enter iotdemo/topic/pub

The last one subscription, this allows traffic from the Lambda to ESP32.

* Click Add Subscription
* The source should be our Lambda - GreengrassHelloWorld
* The target should be our Device - tcci-workshop
* For the topic filter enter freertos/demos/led

Your subscriptions should now look as follows:
[Image: image.png]
## Step 5 - Deploying our group

We now have everything a working group needs.

* Lambdas
* Subscriptions
* Cores
* Devices
* Logging (Settings)

Let’s go ahead and deploy this group.

* Click on Deployments
* Click on Actions, Deploy

[Image: image.png]
* Since this is our first deployment you will be asked some additional questions. This only happens once.
* Select Automatic detection

[Image: image.png]You should now see **Successfully completed** in the **Status** column on the **Deployments** page.
[Image: image.png]
## Step 6 - Create a test client

We’re going to now create our test client acting as the tcci-workshop.
We need to put these files on our device.

* Copy aws_clientcredential.h to **AmazonFreeRTOS\demos\include**
* Copy aws_clientcredential_keys.h to **AmazonFreeRTOS\demos\include**
* Copy DHT22.c to **AmazonFreeRTOS\vendors\espressif\esp-idf\components\driver**
* Copy DHT22.h to **AmazonFreeRTOS\vendors\espressif\esp-idf\components\driver\include\driver**
* Copy CMakeLists.txt to **AmazonFreeRTOS\vendors\espressif\esp-idf\components\driver**
* Copy aws_demo_config.h to **AmazonFreeRTOS\vendors\espressif\boards\esp32\aws_demos\config_files**
* Copy aws_greengrass_discovery_demo.c file to **AmazonFreeRTOS\demos\greengrass_connectivity**

Before running the test, we want to get the MQTT client watching for our test messages.

* Open the AWS IoT console
* Click on test
* Subscribe to #

[Image: image.png]Let’s run the test now.

```
cd AmazonFreeRTOS
cmake -DVENDOR=espressif -DBOARD=esp32_devkitc -DCOMPILER=xtensa-esp32  -GNinja -S . -B build\
.\vendors\espressif\esp-idf\tools\idf.py build
.\vendors\espressif\esp-idf\tools\idf.py -p <COM_PORT> flash
```

You should see a successful connection and message publish. In the browser, you should see the response message and led light up when the vibration happened. Led should be turned off after 5 seconds which triggered by lambda. If you see that then you have everything working.
[Image: image.png]
