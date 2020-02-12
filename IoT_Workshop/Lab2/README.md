# TCCI Workshop Lab2

In this lab we’re we will walk you through setting up and entire AWS Greengrass installation including building a simple Greengrass aware client to communicate with your Greengrass core.


## Step 1 - Getting Started

You’re going to be setting up Greengrass on the hardware provided.

## Step 2 - Setting up the Greengrass Group

Next we’re going to setup the Greengrass group and we’re going to be using the AWS IoT Console to complete these steps. The group is how we control which devices can communicate with core as well as the available Lambdas and logging options.

* Log into the AWS IoT Console and click on Greengrass
* Select, Create first group

### Step 2 - Setting up the Greengrass Group

Next we’re going to setup the Greengrass group and we’re going to be using the AWS IoT Console to complete these steps. The group is how we control which devices can communicate with core as well as the available Lambdas and logging options.

* Log into the AWS IoT Console and click on Greengrass
* Select, Create first group

[Image: image.png]
* Select, Easy Group Creation
* Enter a group name, let’s use TcciWorkshop

[Image: image.png]
* Keep the same core name, TcciWorkshop_Core

[Image: image.png]
* Select, Create Group and Core

The wizard is setting up all the Greengrass dependencies that you need. There are quite a few things being done for you:

* The Group is created
* The Core thing is created, this is the device that is your Greengrass group
* Certificates for your core thing are created for you
* Security policies and logging defaults are also created

On the completion screen you will need to download the package which contains all your certificates and the configuration file for your core.
[Image: image.png]Complete the next steps and then we’ll copy these files to our device.

## Step 3 - Setup your Group Role

You need a role attached to your Greengrass group that gives additional permissions to the core to be able to directly access various AWS services.

* Open the Identity and Access Management console (IAM)
* Click on roles and Create role
* Select AWS Greengrass Role as the Service Role Type

[Image: image.png]
* Select AWSGreengrassResourceAccessPolicy.
* You also want to add the CloudWatchLogsFullAccess policy and click next.

[Image: image.png]
* Enter a name for this role, let’s call it TcciCoreRole

[Image: image.png]
* Click Create Role

Next we’re going to attach this role to our new Greengrass group.

* Open the AWS IoT console
* Click on Greengrass and select the RatchetGroup group
* Click on Settings, you can see we have no Group Role

* Click on Add Role

[Image: image.png]
* Pick our service role, RatchetCoreRole and click Save

[Image: image.png]
## Step 4 - Setup logging

We can setup logging so that all logs on the Greengrass core are sent to CloudWatch logs. This includes the logs from the operation of the core as well as the logs from our Lambda functions which are extremely useful to have.

* Open the AWS IoT console
* Click on Greengrass and select the TcciWorkshop group
* Click on Settings
* Under CloudWatch logs configuration click Edit
* Click on Add another log type

[Image: image.png]
* Select User Lambdas and Greengrass system
* Click Update

[Image: image.png]
* Keep the log settings as Informational

[Image: image.png]
* Click Save

## Step 5 - Installing Greengrass on our device

In the previous step, you downloaded two files to your computer:

* [***greengrass-linux-armv7l-1.9.2.tar.gz***](https://d1onfpft10uf5o.cloudfront.net/greengrass-core/downloads/1.9.2/greengrass-linux-armv7l-1.9.2.tar.gz)***.*** This compressed file contains the AWS IoT Greengrass Core software that runs on the core device.
* ***hash*-setup.tar.gz****.** This compressed file contains security certificates that enable secure communications between AWS IoT and the config.json file that contains configuration information specific to your AWS IoT Greengrass core and the AWS IoT endpoint.

1. Open a terminal on the Raspberry Pi and run the following command.
    `hostname -I`
2. Transfer the two compressed files form your computer to the Raspberry Pi.
    Use a tool such as WinSCP or USB disk to copy files.
3. Open a terminal on the Raspberry Pi and navigate to the folder that contains the compressed files.
4. Decompress the AWS IoT Greengrass Core software and the security resources.
    `sudo tar -xzvf greengrass-linux-armv7l-1.9.2.tar.gz -C /
    sudo tar -xzvf *hash*-setup.tar.gz -C /greengrass`
5. Download the root CA certificate to your core device.
    `cd /greengrass/certs/
    sudo wget -O root.ca.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem`

## Step 7 - Starting the Greengrass core

In this step we’re going to run the core with it’s current configuration. Once we see that it is running we will then deploy the group configuration from the console.
Let’s run Greengrass!

```
`cd /greengrass/ggc/core/
sudo ./greengrassd start`
```

## Step 8 - Deploying the Greengrass Group

Now that you have the core running, let’s deploy a very basic group.

* On your group management page, click on Deployments
* Click on Actions and Deploy

[Image: image.png]

[Image: image.png]
* Your deployment will be packaged up and sent to the core.

Once it is deployed you can move onto the next Greengrass lab. So far you have a functioning core but it doesn’t do much. In the next lab we’ll setup a full group and connect a test device.
**Congrats on getting Greengrass running!**




