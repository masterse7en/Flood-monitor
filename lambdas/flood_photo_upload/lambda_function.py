import json
import os
import hmac
import base64
import time
import boto3

s3 = boto3.client('s3')
BUCKET_NAME = 'YOUR-BUCKET-NAME'   # change to your bucket name

def lambda_handler(event, context):
    try:
        # Reject any request without the correct device key
        expected = os.environ.get('DEVICE_KEY', '')
        headers = event.get('headers') or {}
        sent = headers.get('x-device-key', '')
        if not expected or not hmac.compare_digest(sent, expected):
            return {
                'statusCode': 403,
                'body': json.dumps({'error': 'forbidden'})
            }

        body = event.get('body', '')
        is_b64 = event.get('isBase64Encoded', False)

        if is_b64:
            image_bytes = base64.b64decode(body)
        else:
            # Fallback if Lambda didn't base64 encode it
            image_bytes = body.encode('latin1')

        # Query string carries metadata: ?device_id=...&target=...&distance=...
        qs = event.get('queryStringParameters') or {}
        device_id = qs.get('device_id', 'unknown')
        target = qs.get('target', '0')
        distance = qs.get('distance', '0')
        ts = int(time.time())

        key = f"{device_id}/t{target}_{distance}cm_{ts}.jpg"

        s3.put_object(
            Bucket=BUCKET_NAME,
            Key=key,
            Body=image_bytes,
            ContentType='image/jpeg'
        )

        return {
            'statusCode': 200,
            'body': json.dumps({'status': 'ok', 'key': key})
        }

    except Exception as e:
        print(f"ERROR: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps({'error': str(e)})
        }
