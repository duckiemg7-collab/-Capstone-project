from django.db import models


class ChargerDevice(models.Model):
    """
    Mỗi con ESP32 (trạm sạc)
    """
    device_id = models.CharField(max_length=50, unique=True)
    name = models.CharField(max_length=100, blank=True)

    def __str__(self):
        return self.device_id


class ChargingSession(models.Model):
    """
    Mỗi lần người dùng nạp tiền/sạc là 1 session
    """
    STATUS_CHOICES = [
        ("paid", "Đã thanh toán, chưa gán trạm"),
        ("assigned", "Đang sạc"),
        ("done", "Hoàn tất"),
    ]

    device = models.ForeignKey(
        ChargerDevice, null=True, blank=True, on_delete=models.SET_NULL
    )
    port_no = models.PositiveSmallIntegerField(null=True, blank=True)

    amount_vnd = models.IntegerField()
    status = models.CharField(max_length=20, choices=STATUS_CHOICES)

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    @property
    def device_id_str(self):
        return self.device.device_id if self.device else "N/A"

    def __str__(self):
        return f"#{self.id} {self.device_id_str} P{self.port_no} {self.amount_vnd} {self.status}"


class ChargerPort(models.Model):
    """
    Mỗi cổng sạc trên một ESP32.
    """
    device = models.ForeignKey(ChargerDevice, on_delete=models.CASCADE)
    port_no = models.PositiveSmallIntegerField()
    is_active = models.BooleanField(default=True)

    class Meta:
        unique_together = ("device", "port_no")

    def __str__(self):
        return f"{self.device.device_id} - Port {self.port_no}"
