# The UMTS examples
These examples require an UMTS module (and obviously a working sim card) to connect to mobile networks.
Currently only tested on the LILYGO SIM7000G (ESP32).

Their settings are located in the "Robusto Examples" section in menuconfig, but note that there may be significant settings having to be done for the UMTS module under Robusto Settings > UMTS gateway.

## SMS example
Sends an specifed SMS message to the given recipient.

## MQTT
Publishes MQTT message on an MQTT server. 

## Oauth2 and Google drive
This example goes through the flow for devices with limited input possibilities and uploads a small file to Google Drive. 
Following steps are involved:
1. Create a limited input device app and a OAuth2 credential in the Google Cloud Credentials and OAuth consent screen. 
2. Use the client id and client secret in the setting.
3. Enable the Google Drive API.
4. The default value of the scopes and entry points should then work.
5. Run the example
6. See if a file named Unknown appears in the root folder of you Google Drive.






