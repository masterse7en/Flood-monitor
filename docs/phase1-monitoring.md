# Phase 1: Monitoring

Status: done.

- JSN-SR04T distance, burst median filter, NORMAL / WARNING / DANGER with hysteresis
- OV3660 photos at target distances (50, 100, 150, 200, 250 cm), local web gallery
- Readings to Lambda Function URL, stored in DynamoDB with epoch and IST time
- SNS email alert on every state change
- Photos uploaded to S3 through a second Lambda
- Both Function URLs require an x-device-key header

## Setup

1. Copy firmware/aws_distance_camera/secrets.example.h to secrets.h and fill it in.
2. Arduino IDE board settings (Tools menu):

   | Setting | Value |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | USB CDC On Boot | Disabled (using the UART port) |
   | CPU Frequency | 240MHz (WiFi) |
   | Flash Mode | QIO 80MHz |
   | Flash Size | 16MB (128Mb) |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
   | PSRAM | OPI PSRAM |
   | Upload Mode | UART0 / Hardware CDC |
   | Upload Speed | 921600 |

   These reset when you switch boards. If the camera reports "not ready",
   check PSRAM and Flash Size first.
3. DynamoDB table FloodReadings: partition key device_id (String), sort key timestamp (Number).
4. SNS topic flood-alerts with a confirmed email subscription.
5. S3 bucket for photos, Block all public access on.
6. Lambdas flood_ingest and flood_photo_upload, Python 3.12, timeout 15 s,
   environment variable DEVICE_KEY, Function URL auth NONE.
   Replace the placeholders before deploying: SNS_TOPIC_ARN in flood_ingest,
   BUCKET_NAME in flood_photo_upload. Each function's code goes in its own
   lambda_function.py, with no other .py files, so the default handler runs it.
7. Attach the inline policies in iam/ to each Lambda role.
