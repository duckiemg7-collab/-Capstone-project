import json
import time
import paho.mqtt.client as mqtt
from django.conf import settings
import django
import os

# ===== BẮT BUỘC khi chạy standalone =====
os.environ.setdefault("DJANGO_SETTINGS_MODULE", "smartcharge.settings")
django.setup()
from myapp.models import ChargerDevice, ChargingSession
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT   = 1883
MQTT_TOPIC  = "iot/charge_station/v1/orders"

def on_connect(client, userdata, flags, rc):
    print("MQTT SUB connected with code", rc)
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    print("MQTT STATUS RECEIVED:", payload)

    try:
        data = json.loads(payload)

        device_id = data.get("device")
        port_no   = data.get("port")
        status    = data.get("status")

        if status != "DONE":
            return

        device = ChargerDevice.objects.get(device_id=device_id)

        session = ChargingSession.objects.filter(
            device=device,
            port_no=port_no,
            status="assigned"
        ).last()

        if session:
            session.status = "done"
            session.save()
            print(f"✅ Session {session.id} DONE")

        else:
            print("⚠️ Không tìm thấy session đang sạc")

    except Exception as e:
        print("MQTT error:", e)

def start_mqtt_sub():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()

if __name__ == "__main__":
    start_mqtt_sub()
