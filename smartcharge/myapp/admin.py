from django.contrib import admin
from .models import ChargerDevice, ChargerPort, ChargingSession


@admin.register(ChargerDevice)
class ChargerDeviceAdmin(admin.ModelAdmin):
    list_display = ("device_id", "name")


@admin.register(ChargerPort)
class ChargerPortAdmin(admin.ModelAdmin):
    list_display = ("device", "port_no", "is_active")
    list_filter = ("device",)


@admin.register(ChargingSession)
class ChargingSessionAdmin(admin.ModelAdmin):
    list_display = ("id", "device_id_str", "port_no", "amount_vnd", "status", "created_at")
    list_filter = ("status", "device", "port_no")
    ordering = ("-created_at",)

