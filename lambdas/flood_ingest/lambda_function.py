import json
import os
import hmac
import time
from datetime import datetime, timezone, timedelta
from decimal import Decimal
import boto3
from boto3.dynamodb.conditions import Key

dynamodb = boto3.resource('dynamodb')
table = dynamodb.Table('FloodReadings')
sns = boto3.client('sns')

SNS_TOPIC_ARN = 'arn:aws:sns:YOUR-REGION:YOUR-ACCOUNT-ID:flood-alerts'  # paste your Topic ARN

IST = timezone(timedelta(hours=5, minutes=30))

def get_last_state(device_id):
    resp = table.query(
        KeyConditionExpression=Key('device_id').eq(device_id),
        ScanIndexForward=False,  # newest first
        Limit=1
    )
    items = resp.get('Items', [])
    if items:
        return items[0].get('state', 'NORMAL')
    return None  # no prior reading

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

        body = json.loads(event.get('body', '{}'))

        device_id = body.get('device_id', 'unknown')
        distance_cm = body.get('distance_cm')
        state = body.get('state', 'NORMAL')
        uptime_s = int(body.get('timestamp', 0))  # board uptime, kept for debugging only

        if distance_cm is None:
            return {
                'statusCode': 400,
                'body': json.dumps({'error': 'distance_cm is required'})
            }

        # Real time, stamped when the reading arrives
        now = datetime.now(timezone.utc)
        epoch = int(now.timestamp())
        time_ist = now.astimezone(IST).strftime('%Y-%m-%d %H:%M:%S')

        last_state = get_last_state(device_id)

        # Save the reading first, so an alert problem never loses data
        table.put_item(Item={
            'device_id': device_id,
            'timestamp': epoch,                       # sort key, Unix seconds UTC
            'time_ist': time_ist,                     # readable, India time
            'distance_cm': Decimal(str(distance_cm)), # number, so you can compare
            'state': state,
            'uptime_s': uptime_s
        })

        alert_sent = False
        if last_state is not None and last_state != state:
            message = (
                f"Flood monitor alert\n"
                f"Device: {device_id}\n"
                f"State: {last_state} -> {state}\n"
                f"Distance: {distance_cm} cm\n"
                f"Time (IST): {time_ist}"
            )
            try:
                sns.publish(
                    TopicArn=SNS_TOPIC_ARN,
                    Subject=f"Flood monitor: {state}",
                    Message=message
                )
                alert_sent = True
            except Exception as e:
                print(f"SNS ERROR: {str(e)}")

        return {
            'statusCode': 200,
            'body': json.dumps({'status': 'ok', 'alert_sent': alert_sent})
        }

    except Exception as e:
        print(f"ERROR: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps({'error': str(e)})
        }
