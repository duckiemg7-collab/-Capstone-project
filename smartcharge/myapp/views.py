import json
import requests

from datetime import timedelta

from django.http import JsonResponse
from django.shortcuts import render
from django.views.decorators.csrf import csrf_exempt
from django.contrib.admin.views.decorators import staff_member_required
from django.db import transaction
from django.utils import timezone

import paho.mqtt.publish as mqtt_publish  # <--- THÊM DÒNG NÀY

from .models import ChargingSession, ChargerDevice, ChargerPort

# ====================== MQTT CONFIG ======================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_TOPIC_ORDER = "iot/charge_station/v1/orders"

# ====================== DASHBOARD CHO ADMIN ======================

@staff_member_required  # chỉ user is_staff=True mới vào được
def session_dashboard(request):
    sessions = ChargingSession.objects.order_by('-created_at')[:50]
    return render(request, "myapp/session_dashboard.html", {"sessions": sessions})


# ====================== API CHO ESP32 TEST =======================

@csrf_exempt
def api_report_port(request):
    """
    ESP32 gửi POST:
    {
      "device_id": "ESP32_0001",
      "port_no": 1
    }
    => Tự tạo ChargerDevice + ChargerPort nếu chưa có.
    """
    if request.method != "POST":
        return JsonResponse({"ok": False, "error": "method_not_allowed"}, status=405)

    try:
        data = json.loads(request.body.decode("utf-8"))
    except json.JSONDecodeError:
        return JsonResponse({"ok": False, "error": "invalid_json"}, status=400)

    device_id = data.get("device_id")
    port_no = data.get("port_no")

    if not device_id or port_no is None:
        return JsonResponse({"ok": False, "error": "missing_device_or_port"}, status=400)

    device, _ = ChargerDevice.objects.get_or_create(device_id=device_id)
    port, created = ChargerPort.objects.get_or_create(device=device, port_no=port_no)

    return JsonResponse(
        {
            "ok": True,
            "device_id": device.device_id,
            "port_no": port.port_no,
            "created": created,  # True nếu vừa tạo mới cổng, False nếu đã tồn tại
        }
    )


# ============ HELPER: GÁN SESSION VÀO TRẠM/CỔNG TRỐNG ============

ACTIVE_STATUSES = ["assigned", "charging"]


def assign_session_to_free_slot(session: ChargingSession):
    """
    Tìm (device, port) đang TRỐNG, rồi gán session này vào.
    Trả về session đã gán, hoặc None nếu hết chỗ.
    """
    with transaction.atomic():
        ports = (
            ChargerPort.objects
            .select_for_update()
            .filter(is_active=True)
            .order_by("device__device_id", "port_no")
        )

        for port in ports:
            has_active = ChargingSession.objects.filter(
                device=port.device,
                port_no=port.port_no,
                status__in=ACTIVE_STATUSES,
            ).exists()

            if not has_active:
                session.device = port.device
                session.port_no = port.port_no
                session.status = "assigned"   # Đã gán trạm/cổng
                session.save()
                return session

        # không slot nào trống
        session.status = "waiting"
        session.save()
        return None


def create_session_after_payment(amount_vnd: int):
    """
    Khi thanh toán thành công, gọi hàm này.
    Nó tạo ChargingSession + auto gán vào slot trống (nếu có).
    """
    session = ChargingSession.objects.create(
        amount_vnd=amount_vnd,
        status="paid",   # tạm: vừa thanh toán xong
    )
    assigned = assign_session_to_free_slot(session)
    # Nếu gán được thì trả session đã gán, không thì trả session 'waiting'
    return assigned or session


# ====================== PHẦN THANH TOÁN SEPAY ======================

SEPAY_API_TOKEN = "UTYLIAAPN4GUWDB6B2F0F2PTWNRJR5NPWZOAEGCKZLO5MVM3HEGO88YXQBKJGNJY"
SEPAY_API_URL = "https://my.sepay.vn/userapi/transactions/list"


def payment_view(request):
    return render(request, "myapp/base.html")


def payment_baseline(request):
    """
    Lấy 'mốc' giao dịch mới nhất hiện có cho số tiền đó.
    Frontend sẽ dùng baseline_id này để chỉ check giao dịch mới hơn.
    """
    try:
        amount = float(request.GET.get("amount", "0"))
    except ValueError:
        return JsonResponse({"ok": False, "error": "invalid_amount"}, status=400)

    headers = {
        "Authorization": f"Bearer {SEPAY_API_TOKEN}",
        "Content-Type": "application/json",
    }
    params = {
        "amount_in": amount,
        "limit": 1,  # giao dịch mới nhất với số tiền này
    }

    try:
        resp = requests.get(SEPAY_API_URL, headers=headers, params=params, timeout=8)
        data = resp.json()
    except Exception as e:
        print("SePay baseline error:", e)
        return JsonResponse({"ok": False, "error": "sepay_request_failed"}, status=500)

    txs = data.get("transactions", [])
    baseline_id = None

    if txs:
        try:
            baseline_id = int(txs[0]["id"])
        except Exception:
            baseline_id = None

    return JsonResponse({"ok": True, "baseline_id": baseline_id})


def check_payment(request):
    """
    /api/check-payment/?amount=20000&baseline_id=49682

    Trả về:
    {
        ok: True,
        paid: True/False,
        count: n,
        session: {
            id, device_id, port_no, status
        } hoặc null
    }
    """
    try:
        # luôn ép về int VND cho chắc
        amount = int(float(request.GET.get("amount", "0")))
    except ValueError:
        return JsonResponse({"ok": False, "error": "invalid_amount"}, status=400)

    baseline_id_raw = request.GET.get("baseline_id")
    since_id = None
    if baseline_id_raw:
        try:
            since_id = int(baseline_id_raw) + 1  # > baseline
        except ValueError:
            since_id = None

    headers = {
        "Authorization": f"Bearer {SEPAY_API_TOKEN}",
        "Content-Type": "application/json",
    }
    params = {
        "amount_in": amount,
        "limit": 5,
    }
    if since_id is not None:
        params["since_id"] = since_id

    try:
        resp = requests.get(SEPAY_API_URL, headers=headers, params=params, timeout=8)
        data = resp.json()
    except Exception as e:
        print("SePay check_payment error:", e)
        return JsonResponse({"ok": False, "error": "sepay_request_failed"}, status=500)

    txs = data.get("transactions", [])
    paid = len(txs) > 0

    session_info = None

    if paid:
        # MỖI lần detect có giao dịch mới => tạo 1 session riêng
        session = create_session_after_payment(amount)

        if session is None:
            # Chưa có => tạo mới + gán slot trống
            session = create_session_after_payment(amount)

        if session is not None:
            session_info = {
                "id": session.id,
                "device_id": session.device.device_id if session.device else None,
                "port_no": session.port_no,
                "status": session.status,
            }
                        # ================== GỬI MQTT TẠI ĐÂY ==================
            # Chỉ gửi nếu đã có device + port
            if session.device and session.port_no is not None:
                device_id = session.device.device_id
                port_no = session.port_no
                # amount là số VND, mình ép int luôn cho gọn
                money = int(amount)

                # Format đúng yêu cầu: "deviceID port tiền"
                payload = f"{device_id} {port_no} {money}"

                try:
                    mqtt_publish.single(
                        MQTT_TOPIC_ORDER,
                        payload,
                        hostname=MQTT_BROKER,
                        port=MQTT_PORT,
                    )
                    print("MQTT sent:", MQTT_TOPIC_ORDER, payload)
                except Exception as e:
                    # Không để lỗi MQTT làm hỏng API trả về cho frontend
                    print("MQTT publish error:", e)
            # ======================================================
    return JsonResponse(
        {
            "ok": True,
            "paid": paid,
            "count": len(txs),
            "session": session_info,
        }
    )
