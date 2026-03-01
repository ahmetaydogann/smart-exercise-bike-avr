#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/interrupt.h>

char terminal_buffer[64];
char lcd_buffer[16];

// --- FİZİKSEL SABİTLER VE SİSTEM DEĞİŞKENLERİ ---
float AGIRLIK_KG = 70.0; 
const float MET_DEGERI = 8.0;  
float toplam_kalori = 0.0;
float hedef_kalori = 0.0;      // YENİ: Hedef kalori değişkeni

// DURUM MAKİNESİ (State Machine): 0 = Mola, 1 = Aktif, 2 = Antrenman Bitti
volatile uint8_t sistem_aktif = 1; 

// --- UART (Sanal Terminal) FONKSİYONLARI ---
void uart_init(unsigned int ubrr) {
    UCSR0A &= ~(1 << U2X0);
    UBRR0H = (unsigned char)(ubrr >> 8);
    UBRR0L = (unsigned char)ubrr;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0); 
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}
void uart_transmit(unsigned char data) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = data;
}
void uart_print(const char* str) {
    while (*str) {
        uart_transmit(*str++);
    }
}
unsigned char uart_receive(void) {
    while (!(UCSR0A & (1 << RXC0)));
    return UDR0; 
}
void uart_kullanici_girisi_al(char* buffer, uint8_t max_uzunluk) {
    uint8_t i = 0;
    unsigned char gelen_karakter;
    while (1) {
        gelen_karakter = uart_receive();
        if (gelen_karakter == '\r' || gelen_karakter == '\n') {
            buffer[i] = '\0'; 
            uart_print("\r\n"); 
            break;
        }
        if (gelen_karakter == '\b' && i > 0) {
            i--;
            uart_print("\b \b"); 
        }
        else if (i < max_uzunluk - 1) {
            buffer[i] = gelen_karakter;
            i++;
            uart_transmit(gelen_karakter); 
        }
    }
}

// --- LCD SÜRÜCÜ (PC0-PC3) ---
void lcd_send_nibble(unsigned char nibble) {
    PORTC = (PORTC & 0xF0) | (nibble & 0x0F); 
    PORTB |= (1 << PORTB1); 
    _delay_us(1);
    PORTB &= ~(1 << PORTB1); 
    _delay_us(200);
}
void lcd_command(unsigned char cmd) {
    PORTB &= ~(1 << PORTB0); 
    lcd_send_nibble(cmd >> 4);   
    lcd_send_nibble(cmd & 0x0F); 
    _delay_ms(2);
}
void lcd_data(unsigned char data) {
    PORTB |= (1 << PORTB0); 
    lcd_send_nibble(data >> 4);   
    lcd_send_nibble(data & 0x0F); 
    _delay_ms(2);
}
void lcd_init() {
    _delay_ms(20); 
    PORTB &= ~(1 << PORTB0); 
    lcd_send_nibble(0x03); _delay_ms(5);
    lcd_send_nibble(0x03); _delay_us(100);
    lcd_send_nibble(0x03); _delay_ms(1);
    lcd_send_nibble(0x02); _delay_ms(1); 
    lcd_command(0x28); 
    lcd_command(0x0C); 
    lcd_command(0x01); 
    _delay_ms(2);
}
void lcd_print(const char* str) {
    while (*str) {
        lcd_data(*str++);
    }
}
void lcd_set_cursor(unsigned char row, unsigned char col) {
    unsigned char address = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_command(address);
}

// --- TIMER & KESME AYARLARI ---
void timer_init() {
    TCCR0A = 0x00; TCCR0B = (1 << CS02) | (1 << CS01) | (1 << CS00); TCNT0 = 0;
    TCCR1A = 0x00; TCCR1B = (1 << CS12) | (1 << CS11) | (1 << CS10); TCNT1 = 0;
}
void interrupt_init() {
    EICRA |= (1 << ISC01); EICRA &= ~(1 << ISC00);
    EIMSK |= (1 << INT0);
    sei(); 
}

// INT0 Buton Kesmesi
ISR(INT0_vect) {
    // Sadece sistem bitmemişse (durum 2 değilse) duraklat/başlat yapılabilir
    if (sistem_aktif != 2) {
        sistem_aktif = !sistem_aktif; 
    }
    _delay_ms(50); 
}

void setup() {
  DDRB |= (1 << DDB0) | (1 << DDB1); 
  DDRC |= (1 << DDC0) | (1 << DDC1) | (1 << DDC2) | (1 << DDC3); 
  DDRD &= ~((1 << DDD2) | (1 << DDD4) | (1 << DDD5)); 

  uart_init(103);
  lcd_init();

  // 1. AŞAMA: KİLO GİRİŞİ
  lcd_command(0x01);
  lcd_set_cursor(0, 0); lcd_print("1. KILO GIRISI");
  lcd_set_cursor(1, 0); lcd_print("BEKLENIYOR...");

  uart_print("\r\n--- AKILLI BISIKLET SISTEMINE HOS GELDINIZ --- \r\n");
  uart_print("Kilonuzu girin (kg): ");
  char giris_metni[10];
  uart_kullanici_girisi_al(giris_metni, 10); 
  int girilen_kilo = atoi(giris_metni);
  if (girilen_kilo > 20 && girilen_kilo < 300) AGIRLIK_KG = (float)girilen_kilo;

  // 2. AŞAMA: HEDEF YEMEK/KALORİ MENÜSÜ
  lcd_command(0x01);
  lcd_set_cursor(0, 0); lcd_print("2. HEDEF SECIMI");
  lcd_set_cursor(1, 0); lcd_print("BEKLENIYOR...");

  uart_print("\r\nYakmak istediginiz menuyu secin:\r\n");
  uart_print("1- Kastamonu Tiriti (850 kcal)\r\n");
  uart_print("2- Elbistan Tava (720 kcal)\r\n");
  uart_print("3- Fast Food Burger Menusu (1000 kcal)\r\n");
  uart_print("4- Ozel Kalori Miktari Girecegim\r\n");
  uart_print("Seciminiz (1/2/3/4): ");
  
  uart_kullanici_girisi_al(giris_metni, 5);
  int secim = atoi(giris_metni);

  if (secim == 1) hedef_kalori = 850.0;
  else if (secim == 2) hedef_kalori = 720.0;
  else if (secim == 3) hedef_kalori = 1000.0;
  else {
      uart_print("Yakmak istediginiz kalori miktarini girin (kcal): ");
      uart_kullanici_girisi_al(giris_metni, 10);
      hedef_kalori = (float)atoi(giris_metni);
      if (hedef_kalori <= 0) hedef_kalori = 500.0; // Hatalı girişe karşı varsayılan
  }

  sprintf(terminal_buffer, "\r\n-> SISTEM KURULDU: %.1f kg | Hedef: %.1f kcal\r\n\n", AGIRLIK_KG, hedef_kalori);
  uart_print(terminal_buffer);

  // Sistemi ve Timerları Başlat
  timer_init();
  interrupt_init(); 

  // LCD Ana Ekranı Çiz
  lcd_command(0x01);
  lcd_set_cursor(0, 0); lcd_print("NABIZ:");
  lcd_set_cursor(1, 0); lcd_print("YAKILAN:");
}

void loop() {
  if (sistem_aktif == 1) { // AKTİF DURUM
      uint8_t nabiz_frekans = TCNT0;
      uint8_t pedal_frekans = TCNT1;
      
      TCNT0 = 0; TCNT1 = 0;
      int bpm = nabiz_frekans * 60;
      int rpm = pedal_frekans * 60;

      if (rpm > 0) {
          float saniyelik_kalori = (MET_DEGERI * AGIRLIK_KG * 3.5) / (200.0 * 60.0);
          toplam_kalori += saniyelik_kalori;
      }

      // HEDEF KONTROLÜ (YENİ)
      if (toplam_kalori >= hedef_kalori) {
          sistem_aktif = 2; // Durum: Bitti
      }

      // LCD Güncelleme
      sprintf(lcd_buffer, "%3d BPM  ", bpm); 
      lcd_set_cursor(0, 7); lcd_print(lcd_buffer);

      char kalori_str[6];
      dtostrf(toplam_kalori, 4, 1, kalori_str);
      sprintf(lcd_buffer, "%s kcal  ", kalori_str);
      lcd_set_cursor(1, 8); lcd_print(lcd_buffer);

      sprintf(terminal_buffer, "AKTIF -> Nabiz: %d | Pedal: %d | Yakilan: %s / %.1f kcal\r\n", bpm, rpm, kalori_str, hedef_kalori);
      uart_print(terminal_buffer);
      
      for(int i = 0; i < 100; i++) {
          if (sistem_aktif != 1) break; 
          _delay_ms(10);
      }
  } 
  else if (sistem_aktif == 0) { // MOLA DURUMU
      lcd_set_cursor(0, 7); lcd_print("MOLA... ");
      lcd_set_cursor(1, 8); lcd_print("BEKLIYOR");
      uart_print("DURAKLATILDI - Kalori sayimi durdu.\r\n");
      TCNT0 = 0; TCNT1 = 0; 
      
      for(int i = 0; i < 50; i++) {
          if (sistem_aktif != 0) break; 
          _delay_ms(10);
      }
  }
  else if (sistem_aktif == 2) { // HEDEF TAMAMLANDI DURUMU (YENİ)
      lcd_command(0x01); // Ekranı temizle
      lcd_set_cursor(0, 0); lcd_print(" HEDEF ULASTI!  ");
      lcd_set_cursor(1, 0); lcd_print(" ANTRENMAN BITTI");
      
      uart_print("\r\n************************************************\r\n");
      uart_print("*** TEBRIKLER! HEDEF KALORIYI BARIYLA YAKTINIZ ***\r\n");
      uart_print("************************************************\r\n\n");
      
      TCNT0 = 0; TCNT1 = 0; // Timerları durdur
      
      // Sistem kilitlenir, reset atılana kadar yeniden başlamaz.
      while(1) {
          _delay_ms(1000); // Sonsuz uyku döngüsü
      }
  }
}