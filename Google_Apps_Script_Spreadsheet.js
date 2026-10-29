/**
 * ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * SCRIPT     : Google Apps Script (GAS) untuk Google Spreadsheet
 * DESKRIPSI  : Menerima data JSON dari ESP32 Master Display atau Server Laravel
 *              dan otomatis menambahkan baris baru ke Google Spreadsheet.
 * ==============================================================================
 * PANDUAN PEMASANGAN DI GOOGLE SPREADSHEET:
 * ------------------------------------------------------------------------------
 * 1. Buka Google Spreadsheet baru di browser Anda (sheets.google.com).
 * 2. Buat header di Baris 1:
 *    Kolom A: Timestamp
 *    Kolom B: Node (INDOOR / OUTDOOR)
 *    Kolom C: Laju Dosis (uSv/h)
 *    Kolom D: CPS
 *    Kolom E: Suhu (C)
 *    Kolom F: Kelembapan / Tekanan
 *    Kolom G: Status Relay
 *    Kolom H: RSSI (dBm)
 * 3. Klik menu: Ekstensi (Extensions) -> Apps Script.
 * 4. Hapus seluruh isi kode lama, lalu Tempel (Paste) kode skrip ini di sana.
 * 5. Klik Simpan (Ikon Disket) -> Klik tombol Deploy (Terapkan) -> "New deployment".
 * 6. Pilih tipe: "Web app" (Aplikasi Web):
 *    - Description: "Radiation Monitor Webhook"
 *    - Execute as: "Me" (Email Google Anda)
 *    - Who has access: "Anyone" (Siapa saja, agar ESP32 bisa kirim tanpa login)
 * 7. Klik Deploy -> Salin "Web App URL" yang dihasilkan.
 * 8. Tempelkan URL tersebut ke variabel `GOOGLE_SHEET_URL` di ESP32_Master_Display.ino!
 * ==============================================================================
 */

function doPost(e) {
  try {
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    var rawData = e.postData.contents;
    var data = JSON.parse(rawData);

    var timestamp = Utilities.formatDate(new Date(), "Asia/Jakarta", "yyyy-MM-dd HH:mm:ss");
    
    // Identifikasi apakah data berasal dari INDOOR atau OUTDOOR
    var node = data.node || (data.seq !== undefined ? "INDOOR" : "OUTDOOR");
    var usvh = data.usvh || 0;
    var cps = data.cps || 0;
    var suhu = data.suhu || 0;
    var kelembapanAtauTekanan = data.kelembapan || data.tekanan || 0;
    var relay = data.relay || "-";
    var rssi = data.rssi || "-";

    // Tambahkan baris baru ke Spreadsheet
    sheet.appendRow([
      timestamp,
      node,
      usvh,
      cps,
      suhu,
      kelembapanAtauTekanan,
      relay,
      rssi
    ]);

    return ContentService.createTextOutput(JSON.stringify({
      "status": "success",
      "message": "Data berhasil dicatat ke spreadsheet"
    })).setMimeType(ContentService.MimeType.JSON);

  } catch (error) {
    return ContentService.createTextOutput(JSON.stringify({
      "status": "error",
      "message": error.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

function doGet(e) {
  return ContentService.createTextOutput("Radiation Monitor Google Spreadsheet Webhook Aktif!");
}
