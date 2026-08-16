# microFX Error App

Firmware-owned JavaScript error screen displayed when an uploaded project fails its
activation health check. The supervisor passes the latest single-line QuickJS
exception through `MICROFX_ERROR_DETAIL`; the scene wraps it as white text on a
black background and remains visible until the next Save & Run request.
