# KDatalogger

Datalogger industriale su ESP32-S3 (ESP-IDF + FreeRTOS). Acquisisce temperature da 8 termocoppie
(MAX31855), 5 ingressi digitali e 5 ingressi analogici, registra su flash interna (FAT,
wear-levelling) ed espone i log al PC via USB Mass Storage. Ha un display TFT locale con GUI LVGL
per visualizzare canali, grafici e impostazioni senza bisogno di un host.

## Hardware

- MCU: ESP32-S3 (WROOM-1-N8R2, 8 MB flash, 2 MB PSRAM Quad SPI)
- Display: pannello SPI ST7796 480×320, GUI LVGL via `esp_lvgl_port`
- Nessun RTC hardware: l'orologio è impostato manualmente dall'operatore a ogni accensione
  (componente `timekeeping`), UTC0 fisso, senza persistenza dello stato "valido" tra i boot

### Pin assignment

**Termocouple bus (MAX31855, SPI3_HOST, no DMA, CS software)**

| Segnale | GPIO |
| --- | --- |
| SCLK | 36 |
| MISO | 37 |
| CS canale 0–7 | 1, 2, 42, 41, 40, 39, 38, 35 |

**Ingressi digitali** (GPIO 15, 16, 17, 18, 3)

**Ingressi analogici** (GPIO 9, 7, 6, 5, 4) — AI1 (GPIO13) non fa parte di questo gruppo: è
riassegnato a ingresso digitale "record enable" (livello alto = logging attivo, vedi sotto).

**Display (SPI2_HOST)**

| Segnale | GPIO |
| --- | --- |
| MOSI | 11 |
| SCLK | 12 |
| CS | 10 |
| DC | 8 |
| RST | 14 |
| Backlight (LEDC) | 21 |

**USB nativo** — D+/D- sugli IO19/IO20 dell'ESP32-S3, stessa porta USB-C usata per alimentazione
5 V da PC e per l'esposizione USB MSC.

## Architettura firmware

- `app_core` — orchestratore, avvia i servizi da `app_main`
- `board` — HAL con l'assegnazione pin reale (nessun placeholder)
- `max31855` — driver SPI3_HOST, 8 canali, CS manuali via GPIO
- `digital_inputs` / `analog_inputs` — driver GPIO/ADC oneshot con calibrazione
- `acquisition` — task FreeRTOS periodico (periodo configurabile, vedi `settings`) che campiona
  tutti i driver in un `kdl_sensor_sample_t` (`data_model`)
- `logger` — scrive i campioni in CSV su FAT (`/data/logs`), flush periodico con fflush+fsync
- `storage` — partizione FAT con wear-levelling, arbitraggio di accesso FIRMWARE/USB_HOST
- `usb_msc` — espone la partizione `storage` come Mass Storage Device (TinyUSB)
- `timekeeping` — orologio software impostato dall'operatore, nessuna batteria tampone
- `settings` — parametri persistenti in NVS (periodo di acquisizione, luminosità display)
- `display` / `gui` — driver ST7796 + pagine LVGL (main, grafico, impostazioni, data/ora, USB,
  splash), gestione pulsanti con debounce

### Registrazione condizionata da GPIO

Il logging non parte automaticamente all'accensione: segue il livello dell'ingresso "record
enable" (AI1 / GPIO13) — alto = logging ON, basso = OFF. Serve un comando esterno (interruttore o
segnale) per avviare/fermare la sessione senza passare dalla GUI.

### USB MSC: accesso esclusivo

La partizione `storage` non può essere scritta contemporaneamente da firmware e host USB:

- USB scollegato all'accensione → il firmware accede alla partizione (acquisizione/logging attivi).
- USB collegato all'accensione, o collegato durante il funzionamento → il PC vede la partizione
  come disco rimovibile; il firmware non può più scriverci finché non viene rimossa/espulsa lato
  PC (o scollegata fisicamente).

## Build

Richiede ESP-IDF (v6.x) con target `esp32s3` configurato. Il progetto usa una partition table
custom (`partitions.csv`) e PSRAM Quad — entrambi già impostati in `sdkconfig.defaults`.

```bash
idf.py set-target esp32s3
idf.py build
```

## Programmazione (Flashing)

Il dispositivo va programmato via USB-C nativo dell'ESP32-S3 — non ha (ne' serve) un connettore
JTAG/UART dedicato. Il chip entra in bootloader mode automaticamente quando il tool di flashing lo
richiede (reset via USB-Serial-JTAG nativo, nessun pulsante BOOT da tenere premuto in condizioni
normali). Se il collegamento resta bloccato su "Connecting...", verificare che GPIO0 non sia
vincolato da altro hardware sulla scheda.

Partizionamento (`partitions.csv`, custom): bootloader a `0x0`, partition table a `0x8000`, app
(`factory`) a `0x10000`.

### Opzione A — `idf.py flash` (ambiente ESP-IDF gia' installato)

```bash
idf.py -p PORT flash monitor
```

Compila e flasha in un unico passaggio. E' il metodo da usare durante lo sviluppo.

### Opzione B — `esptool.py` standalone (senza toolchain ESP-IDF completa)

Installazione (richiede Python 3.8+):

```bash
pip install esptool
esptool.py version   # verifica installazione
```

Servono comunque i binari compilati (`idf.py build`, oppure quelli gia' presenti in `build/` da una
build precedente). Flash manuale specificando i tre binari con i relativi offset:

```bash
esptool.py --chip esp32s3 -p PORT -b 460800 write_flash \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/KDatalogger.bin
```

Sostituire `PORT` con la porta seriale (`/dev/cu.usbmodem*` su macOS, `/dev/ttyACM0` su Linux,
`COMx` su Windows).

Monitor seriale dopo il flash:

```bash
esptool.py --chip esp32s3 -p PORT monitor
```

### Opzione C — ESP Web Tools / esptool-js (dal browser, nessuna installazione)

Richiede un browser Chromium (Chrome o Edge) — Firefox e Safari non supportano la Web Serial API.

1. Collegare il dispositivo via USB-C.
2. Aprire https://espressif.github.io/esptool-js/
3. "Connect" e selezionare la porta seriale.
4. Nella sezione "Program", compilare le righe file + offset (pulsante "Add File" per aggiungerne
   altre):
   - `bootloader/bootloader.bin` → `0x0`
   - `partition_table/partition-table.bin` → `0x8000`
   - `KDatalogger.bin` → `0x10000`
5. "Program" e attendere il completamento.

Utile per riprogrammare un'unita' sul campo senza toolchain ESP-IDF installata.
