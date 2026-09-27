# KDatalogger

Datalogger industriale su ESP32-S3 (ESP-IDF + FreeRTOS). Acquisisce temperature da 8 termocoppie
(MAX31855), 5 pressioni da sensori in tensione e il regime motore, registra su flash interna (FAT,
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

**Ingressi IN1–IN7** (schema Datalogger V3, `docs/Datalogger-v3.pdf`: partitore 10k/15k,
rapporto 0,6, clamp BAT54S; fino a 5 V al morsetto):

| Ingresso | GPIO | Uso |
| --- | --- | --- |
| IN1 | 13 | "record enable" digitale (livello alto = logging attivo, vedi sotto) |
| IN2 | 46 | contagiri, cattura MCPWM (GPIO46 non ha ADC; è un pin di strapping) |
| IN3–IN7 | 9, 7, 6, 5, 4 | pressioni, ADC oneshot con media di 16 conversioni |

**Canali** — nomi e unità sono in `components/data_model/data_model_channels.c`, condivisi da
display e log (modificarli richiede un nuovo firmware):

| Ingresso | Sigla | Grandezza |
| --- | --- | --- |
| Tc1–Tc4 | `Cil 1`–`Cil 4` | temperatura cilindro 1–4 |
| Tc5 / Tc6 | `IC in` / `IC out` | temperatura ingresso / uscita intercooler |
| Tc7 / Tc8 | `Olio` / `Acqua` | temperatura olio / acqua |
| IN2 | `Giri` | regime motore [rpm] |
| IN3 / IN4 | `P IC in` / `P IC out` | pressione turbo ingresso / uscita intercooler [bar] |
| IN5 | `P scar` | pressione gas di scarico [bar] |
| IN6 / IN7 | `P benz` / `P olio` | pressione benzina / olio [bar] |

La scala dei sensori non è nel firmware: si imposta nel file `sensori.ini`, vedi sotto.

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
- `digital_inputs` / `analog_inputs` — driver dei pulsanti (GPIO) e ADC oneshot con calibrazione
  e media
- `tachometer` — cattura MCPWM dei periodi tra impulsi del contagiri (ISR in IRAM, attiva anche
  durante le scritture su flash: `CONFIG_MCPWM_ISR_CACHE_SAFE`)
- `sensors` — scala delle pressioni (`pressure_scaling`), regime da periodi (`tach_math`) e file
  di configurazione `sensori.ini` (`sensor_config`); i primi tre moduli sono C puro, testati su PC
- `acquisition` — task FreeRTOS periodico (periodo configurabile, vedi `settings`) che campiona
  tutti i driver in un `kdl_sensor_sample_t` (`data_model`)
- `logger` — scrive i campioni in CSV su FAT (`/data/logs`), flush periodico con fflush+fsync
- `storage` — partizione FAT con wear-levelling, arbitraggio di accesso FIRMWARE/USB_HOST
- `usb_msc` — espone la partizione `storage` come Mass Storage Device (TinyUSB)
- `timekeeping` — orologio software impostato dall'operatore, nessuna batteria tampone
- `settings` — parametri persistenti in NVS (periodo di acquisizione, luminosità display, zero
  delle pressioni)
- `display` / `gui` — driver ST7796 + pagine LVGL (main, grafico, impostazioni, data/ora, USB,
  splash), gestione pulsanti con debounce

### Registrazione condizionata da GPIO

Il logging non parte automaticamente all'accensione: segue il livello dell'ingresso "record
enable" (IN1 / GPIO13) — alto = logging ON, basso = OFF. Serve un comando esterno (interruttore o
segnale) per avviare/fermare la sessione senza passare dalla GUI.

### Configurazione sensori (`sensori.ini`)

I sensori li sceglie l'utente, quindi la loro scala sta in un file di testo alla radice del disco
USB, `sensori.ini`, da modificare con il Blocco note. Il firmware lo legge all'accensione e a ogni
uscita dalla modalità USB; se manca ne scrive uno commentato con i valori iniziali (pressioni
disattivate, contagiri attivo a 1 impulso/giro, fondo scala 6000 rpm).

- Una sezione per ingresso, `[IN3]`…`[IN7]`: `attivo`, `tensione_min`, `tensione_max`,
  `pressione_min`, `pressione_max` (dalla scheda tecnica del sensore). Decimali con virgola o
  punto.
- `[GIRI]`: `attivo`, `impulsi_giro` (anche decimale, es. morsetto W dell'alternatore),
  `giri_max` (fondo scala della barra e filtro disturbi: impulsi più vicini di metà periodo a
  `giri_max` sono scartati).
- Una sezione con un errore (valore non leggibile, sensore con uscita oltre i 5 V misurabili) viene
  disattivata; il piè di pagina di Impostazioni mostra `sensori.ini: errore riga N`.

Diagnostica pressioni: con sensori a zero vivo (0,5–4,5 V, 1–5 V) un filo interrotto o un corto
dà `ERR` sul display e cella vuota nel log. Con sensori 0–5 V non è rilevabile (si legge 0 bar).
**Impostazioni → Zero pressioni** prende la lettura attuale come 0 bar (sensori all'aria, motore
spento); un ingresso che legge più del 5 % del fondo scala non viene azzerato, così uno zero dato
sotto pressione o su un sensore assoluto non si salva. Lo zero è in NVS, in volt.

Contagiri: il regime viene dalla media dei periodi misurati nel ciclo, non dal conteggio degli
impulsi (a 1 impulso/giro e 1000 rpm, contare su 500 ms darebbe ±12 %). Senza impulsi il valore
scende in base al tempo dall'ultimo impulso e va a 0 sotto 100 rpm.

### Formato dei file di log

Ogni sessione di registrazione crea `/data/logs/log_NNNN.csv`. Il file è pensato per essere aperto
con doppio click in Excel italiano: separatore `;`, decimale `,`, UTF-8 con BOM.

Spazio: la partizione dati è di circa 5 MB. Il logger cancella da solo i log più vecchi quando lo
spazio libero scende sotto 256 KB (controllo all'avvio della sessione e a ogni flush, ogni 2 s) o i
file superano 500: la registrazione non si ferma mai per disco pieno. Se un file non si apre o una
scrittura fallisce, la barra di stato mostra `ERRORE LOG` finché la sessione successiva non parte
correttamente. Nota macOS: i file cancellati dal Finder finiscono in `.Trashes` sul disco del
datalogger e occupano spazio finché non si svuota il Cestino con il disco collegato.

```
Data;Ora;Tempo [s];Cil 1 [°C];…;Acqua [°C];P IC in [bar];…;P olio [bar];Giri [rpm]
23/09/2026;14:32:05;0,00;85,25;…;;1,23;…;;2150
```

- `Data`/`Ora` sono vuote se l'orologio non è stato impostato dopo l'accensione.
- `Tempo [s]` parte da 0 al primo campione della sessione: è l'asse X per i grafici.
- Un canale non valido (termocoppia aperta/in corto, pressione disattivata o in guasto, contagiri
  disattivato) è una cella vuota.
- IN1 non compare: è l'ingresso record-enable.

### USB MSC: accesso esclusivo

La partizione `storage` non può essere scritta contemporaneamente da firmware e host USB:

- Fuori dalla modalità USB il dispositivo è staccato dal bus (`tud_disconnect()`): anche col cavo
  collegato il PC non lo vede, quindi non può scrivere sulla partizione né riprendersela mentre il
  logger registra.
- Il pulsante USB sull'HMI (con registrazione ferma) collega il dispositivo al PC, che vede la
  partizione come disco rimovibile; acquisizione e logging sono fermi.
- Si esce espellendo il disco dal PC: il firmware riceve l'eject e torna da solo in modalità
  normale. "Esci" sull'HMI senza eject mostra un avviso e richiede una seconda pressione entro
  5 s: è la via d'uscita se il PC si è bloccato o il cavo è stato staccato (la scheda non rileva
  VBUS, quindi lo scollegamento non sempre si vede).

### Aggiornamento firmware da USB

`idf.py build` produce, oltre a `build/KDatalogger.bin`, la copia `build/KDatalogger_v<versione>.bin`
(versione da `version.txt`, da incrementare a ogni rilascio). Per aggiornare:

1. Modalità USB dall'HMI, copiare il file `.bin` nella root del disco, espellere il disco dal PC.
2. Al rientro dalla modalità USB (e a ogni accensione) il datalogger cerca il file, lo installa
   nello slot OTA inattivo mostrando l'avanzamento, lo cancella e si riavvia.
3. La versione in uso è sullo splash di avvio (in basso a destra) e in fondo alla pagina
   Impostazioni: dopo il riavvio mostra già quella nuova. Viene confermata dopo 15 s continuativi
   di funzionamento normale (volume leggibile, acquisizione che produce campioni); se nel frattempo
   si blocca, si resetta o fallisce il controllo, il bootloader torna alla versione precedente e il
   display lo segnala fino a "OK". Un aggiornamento riuscito non mostra messaggi.

Regole (`components/fw_update`):

- Il file si riconosce dal contenuto, non dal nome: header ESP32-S3 e project name `KDatalogger`.
  Qualsiasi altro `.bin` resta dov'è, ignorato, come i file `._*` che macOS scrive sul disco.
- File troncato o danneggiato (anche per un disco scollegato senza espellerlo): rinominato in
  `.bad`, firmware invariato. File identico al firmware in uso: cancellato. Più file validi:
  nessuna installazione finché non ne resta uno.
- Mai durante una registrazione: se IN1 è attivo, l'aggiornamento aspetta la fine della sessione.
- Il confronto è sull'immagine (SHA dell'ELF), non sul numero di versione: una build diversa con la
  stessa versione viene installata comunque, e si può anche tornare a una versione precedente.
- Nessuna firma: il controllo evita file sbagliati o incompleti, non protegge da manomissioni.
  Un firmware che supera l'autotest ma non sa più aggiornarsi va ripristinato via seriale.

## Build

Richiede ESP-IDF (v6.x) con target `esp32s3` configurato. Il progetto usa una partition table
custom (`partitions.csv`) e PSRAM Quad — entrambi già impostati in `sdkconfig.defaults`.

```bash
idf.py set-target esp32s3
idf.py build
```

### VS Code

`.vscode/settings.json` non è versionato: l'estensione ESP-IDF ci scrive valori legati alla
macchina (porta seriale, percorso di ESP-IDF) e li riscrive a ogni selezione della porta.
Sono versionati `c_cpp_properties.json` e `launch.json`. Su un clone nuovo basta impostare dal
menu dell'estensione il target `esp32s3`, la porta e la configurazione OpenOCD
(`interface/ftdi/esp_ftdi.cfg`, `target/esp32s3.cfg`).

Per programmare conviene l'adattatore USB-seriale sulla UART0 invece della USB nativa: la USB
nativa passa a TinyUSB quando il disco è ceduto al PC, e con lei sparirebbe anche la console.

### Test su PC

I moduli di `components/sensors` senza dipendenze ESP-IDF (scala pressioni, regime, parser di
`sensori.ini`) hanno test Unity che girano sul PC. Unity viene preso dall'albero ESP-IDF, quindi
serve `IDF_PATH` (è impostato con l'ambiente ESP-IDF attivo):

```bash
cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
```

## Programmazione (Flashing)

Il dispositivo va programmato via USB-C nativo dell'ESP32-S3 — non ha (ne' serve) un connettore
JTAG/UART dedicato. Il chip entra in bootloader mode automaticamente quando il tool di flashing lo
richiede (reset via USB-Serial-JTAG nativo, nessun pulsante BOOT da tenere premuto in condizioni
normali). Se il collegamento resta bloccato su "Connecting...", verificare che GPIO0 non sia
vincolato da altro hardware sulla scheda.

Partizionamento (`partitions.csv`, custom): bootloader a `0x0`, partition table a `0x8000`,
`otadata` a `0xd000`, due slot app OTA da 1,5 MB (`ota_0` a `0x10000`, `ota_1` a `0x190000`), dati
FAT (`storage`) da `0x310000` a fine flash (8 MB).

Una scheda programmata con la tabella precedente (`factory` da 2 MB) va cancellata per intero
una volta (`idf.py -p PORT erase-flash flash`): lo storage cambia offset e va riformattato, quindi
i log presenti vanno salvati prima.

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
build precedente). Flash manuale specificando i quattro binari con i relativi offset:

```bash
esptool.py --chip esp32s3 -p PORT -b 460800 write_flash \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xd000  build/ota_data_initial.bin \
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
   - `ota_data_initial.bin` → `0xd000`
   - `KDatalogger.bin` → `0x10000`
5. "Program" e attendere il completamento.

Utile per riprogrammare un'unita' sul campo senza toolchain ESP-IDF installata.
