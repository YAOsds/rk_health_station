#include <assert.h>
#include <string.h>

#include "provisioning_html.h"

int main(void)
{
    assert(strstr(RK_PROVISIONING_HTML, "<select name=\"wifi_ssid\"") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "fetch('/scan')") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "setInterval(refreshWifiList, 3000)") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "未扫描到可用 Wi-Fi") != NULL);
    return 0;
}
