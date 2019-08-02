# TCCI Workshop Lab1

In this lab you will setup an IoT “Thing” in AWS IoT Core, once you have that setup you will run a small program to simulate data being sent to AWS IoT Core and then use the MQTT Test Client to view each MQTT message payload.


## Architecture

[Image: image.png]
## Step 1 - Creating your first “Thing”, security policies and certificates.

Let’s get your account setup with a new Thing, certificates and security policies.
Log into your AWS console and make sure you can access the AWS IoT dashboard. It should like the following:
[Image: image.png]Let’s create our first “thing” and setup the policy and certificates for this to work.

## Step 2 - FreeRTOS Wizard

In the above screenshot you’ll see a **“Software”** menu option. Select this to continue.
You’ll now see this screen:
[Image: image.png]We’re going to select **“Amazon FreeRTOS Device Software”** - click the configure download button.
Next we’re going to pick our target hardware platform. This is used to generate a full package for us to quickly connect to AWS IoT.
[Image: image.png]For the hardware we’re working on today let’s pick **ESP32-DevKitC** as our platform.
You will now see this screen
[Image: image.png]
## Step 3 - Creating Things

Ok so now we’re ready to get started. Click getting started and then download .
Download the FreeRTOS zip file containing all your certificates.
[Image: image.png]
> Note - Do not lose this zip file, it contains your private key file.

Click **Create and download** then enter a name for your new Thing.
[Image: image.png]For these labs let’s call our new thing, **tcci-workshop**. Enter this name and click **Next step.**
On the next screen you can see that everything has been generated for you!
[Image: image.png]So let’s see exactly what was generated.

* You’ll notice a security policy has been created for you allowing you to immediately send and receive messages.
* A aws_clientcredential.h has been created, this header file will be used to network configuration.
* Finally, a Credentials zip file containing all your certificates.

> Note - Do not lose this zip file, it contains your private key file which cannot be retrieved again.

Click **D****ownload and continue** then follow the instruction to configure your Wi-Fi.
[Image: image.png]Unzip the Credentials.zip and open the aws_clientcredential.h file. Modify the following define.

* clientcredentialWIFI_SSID as your network’s SSID
* clientcredentialWIFI_PASSWORD as your network’s password
* clientcredentialWIFI_SECURITY as your network's security type

[Image: image.png]Click **Test**** **then waiting for messages from your device.

* Click Done to complete the Wizard

[Image: image.png]
## Step 4 - Adjust our “Thing” Security Policy

The default security policy created by the above wizard will limit the resources your device can connect on. For the labs in this workshop we’re going to create a more open policy. So we need to find and edit the policy that has been created already.

* In the IoT Console click on **Manage** - it will default to Things.
* Find the thing you just created, in this case look for **tcci-workshop**.
* Click on your device to see it’s details.
* Click on **Security**.
* Click on the attached certificate - see below

[Image: image.png]
* You will see your certificate details.
* Click on **Policies**

[Image: image.png]
* Click on your policy, usually that’s **tcci-workshop****-Policy**.
* Click **Edit Policy Document**
* [Image: image.png]
* Enter the following for your document.

```
`{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:Publish",
        "iot:Subscribe",
        "iot:Connect",
        "iot:Receive",
        "greengrass:*"
      ],
      "Resource": [
        "*"
      ]
    }
  ]
}`
```

* Click **Save as new version**
* [Image: image.png]

That’s it! your device can now publish and subscribe to any topics.

## Step 5 - Quick Review

Let’s have a quick review.

* Your certificates have been created and activated for you.
* A security policy has been created and modified for the access we need.
* The certificate and security policy have been attached to the thing “tcci-workshop” that you created.

The above are the three requirement components to use AWS IoT.

## Step 6 - Copy files to your device

We need to put these files on our device.

* Copy aws_clientcredential.h to **AmazonFreeRTOS\demos\include**
* Copy aws_clientcredential_keys.h to **AmazonFreeRTOS\demos\include**
* Copy DHT22.c to **AmazonFreeRTOS\vendors\espressif\esp-idf\components\driver**
* Copy DHT22.h to **AmazonFreeRTOS\vendors\espressif\esp-idf\components\driver\include\driver**
* Copy CMakeLists.txt to **AmazonFreeRTOS\vendors\espressif\esp-idf\components\driver**
* Copy aws_demo_config.h to **AmazonFreeRTOS\vendors\espressif\boards\esp32\aws_demos\config_files**
* Copy iot_demo_mqtt.c file to **AmazonFreeRTOS\demos\mqtt**

## Step 7 - Test

Now you can run the lab. Check your com port in device manager before you flash device.
[Image: image.png]
```
cd AmazonFreeRTOS
cmake -DVENDOR=espressif -DBOARD=esp32_devkitc -DCOMPILER=xtensa-esp32  -GNinja -S . -B build\
.\vendors\espressif\esp-idf\tools\idf.py build
.\vendors\espressif\esp-idf\tools\idf.py -p <COM_PORT> flash
```

To check and see if your message was published to the message broker go to the MQTT Client and subscribe to the iot topic and you should see your JSON Payload.

* Open the IoT Console
* Click on **Test**
* Subscribe to **#**

[Image: image.png]You will receive the temperature and humidity data from your device.
[Image: image.png]Publish to **iotdemo/topic/sub** with the payload can turn off the LED.

* {“led”: “off”}

[Image: image.png]
