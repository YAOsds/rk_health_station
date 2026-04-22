#include <assert.h>
#include <string.h>

#include "provisioning_html.h"

int main(void)
{
    assert(strstr(RK_PROVISIONING_HTML, "<select name=\"wifi_ssid\"") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "fetch('/scan')") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "setInterval(refreshWifiList, 3000)") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "未扫描到可用 Wi-Fi") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "let stickySelectedSsid=''") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "addEventListener('change'") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "stickySelectedSsid=event.target.value") != NULL);
    assert(strstr(RK_PROVISIONING_HTML, "当前选择的 Wi-Fi 暂未出现在本轮扫描结果中") != NULL);
    return 0;
}
