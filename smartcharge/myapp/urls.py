from django.urls import path
from . import views

app_name = "myapp"

urlpatterns = [
    path("", views.payment_view, name="home"),
    path("api/payment-baseline/", views.payment_baseline, name="payment_baseline"),
    path("api/check-payment/", views.check_payment, name="check_payment"),
    path("api/charger/report-port/", views.api_report_port, name="api_report_port"),
    path("dashboard/sessions/", views.session_dashboard, name="session_dashboard"),
]

