/* Host test linked with the actual built nvdp-connector.o; no GPU access. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "nvkms-types.h"
#include "dp/nvdp-connector.h"
#include "nvdp-connector-event-sink.hpp"

static NVDevEvoRec dev;
static NVDispEvoRec disp;
static NVConnectorEvoRec connector;
static NVDpyEvoRec dpy;
static NVDPLibConnectorRec dp;
static NVDPLibDeviceRec sink;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #condition); \
        std::exit(1); \
    } \
} while (0)

/* Kernel object uses an external return thunk; the host harness uses RET. */
__asm__(".text\n.globl __x86_return_thunk\n__x86_return_thunk:\nret\n");

static void reset(void)
{
    std::memset(&dev, 0, sizeof(dev));
    std::memset(&disp, 0, sizeof(disp));
    std::memset(&connector, 0, sizeof(connector));
    std::memset(&dpy, 0, sizeof(dpy));
    std::memset(&dp, 0, sizeof(dp));
    std::memset(&sink, 0, sizeof(sink));
    disp.pDevEvo = &dev;
    connector.pDispEvo = &disp;
    connector.pDpLibConnector = &dp;
    connector.legacyType = NV0073_CTRL_SPECIFIC_DISPLAY_TYPE_DFP;
    connector.dfpInfo = DRF_DEF(0073, _CTRL_DFP_FLAGS, _SIGNAL, _DISPLAYPORT);
    dpy.pDispEvo = &disp;
    dpy.pConnectorEvo = &connector;
    dpy.id = nvNvU32ToDpyId(0x2000);
    dp.pConnectorEvo = &connector;
}

int main(void)
{
    reset();
    CHECK(!nvDPIsDpyActivationAllowed(&dp, &dpy));
    dp.plugged = TRUE;
    CHECK(nvDPIsDpyActivationAllowed(&dp, &dpy));

    dp.plugged = FALSE;
    disp.connectedDisplays = nvAddDpyIdToEmptyDpyIdList(dpy.id);
    dpy.parsedEdid.valid = TRUE;
    CHECK(!nvDPIsDpyActivationAllowed(&dp, &dpy));

    dp.clientForcedConnected = TRUE;
    CHECK(nvDPIsDpyActivationAllowed(&dp, &dpy));

    reset();
    connector.dfpInfo |=
        DRF_DEF(0073, _CTRL_DFP_FLAGS, _EMBEDDED_DISPLAYPORT, _TRUE);
    CHECK(nvDPIsDpyActivationAllowed(&dp, &dpy));

    reset();
    disp.displayPortMSTIds = nvAddDpyIdToEmptyDpyIdList(dpy.id);
    dp.clientForcedConnected = TRUE;
    CHECK(!nvDPIsDpyActivationAllowed(&dp, &dpy));
    dp.plugged = TRUE;
    CHECK(!nvDPIsDpyActivationAllowed(&dp, &dpy));
    dpy.dp.pDpLibDevice = &sink;
    CHECK(!nvDPIsDpyActivationAllowed(&dp, &dpy));
    sink.isPlugged = TRUE;
    CHECK(nvDPIsDpyActivationAllowed(&dp, &dpy));
    dp.plugged = FALSE;
    CHECK(!nvDPIsDpyActivationAllowed(&dp, &dpy));

    std::puts("PASS: absent/connected SST, stale EDID and display cache, "
              "forced SST, embedded panel, MST upstream and per-sink presence");
    return 0;
}
