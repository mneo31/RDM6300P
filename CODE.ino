#include <SoftwareSerial.h>
#include <avr/pgmspace.h> //on n'oublie pas d'intégrer la bibliothèque de gestion de mémoire
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 8, 2); // set the LCD address to 0x20 for a 16 chars and 2 line display

#define taillekey       13           //nombre d'octet de la clef
#define buzzer          6           //defini la broche du buzzer
#define serialrfid      11         // broche du rdm6300 rfid 125K
#define lumiere         12          //broche commande relais lumière
#define alarme          10          //broche commande relais alarme maison
#define sirene          7           //broche sortie sirene
#define ledRouge        4           //broche led rouge
#define ledVerte        5           //broche led verte
#define contact         3           //broche du contact NF de la porte (détecteur d'ouverture)
#define RAZ             2           // broche de remise a zero de l'EEprom (test seulement)

SoftwareSerial rfidserial(serialrfid, 22); // Digital pins 2 and 3 connect to pins 1 and 2 of the RMD6300

//#define debugcode

byte rfid[14];//tableau des octets du rfid (lecture)
byte rom[taillekey];//tableau des octets du block EEPROM (lecture)
byte etat = 0; //résulat de la lecteur rfid 0 = clef inconnu, 1 = clef master, 2... = clef connu
byte addrkey = 0; //adresse de la clef lu (si connu, sinon 0);
volatile byte state = 5;
boolean taglu; // variable taglu vrai pour lu sinon non
boolean endrfidserial = false;// variable pour stop softserial
unsigned long timerfidserial = 0; //variable de temps pour debounce rfidserial
unsigned long timerajouterkey = 0; //variable de temps pour debounce ajouterkey
unsigned long minuteur = 0; //variable de temps pour le minuteur de gestion de l'état de l'alarme (state) void gestion
unsigned long prevmillis = 0; //variable de temps pour le minuteur de gestion de l'état de l'alarme (state) void gestion
unsigned long tempobeep = 0; //variable pour gestion du temps du beep du buzzer
unsigned long tempoext = 0; //variable pour temporisation alarme maison (commande relais)
unsigned long tempoled = 0; // variable pour temporisation ledrouge ( redled byte byte )
unsigned long temposirene = 0;// variable pour temporisation variation fréquence de la sirene
unsigned long tempolumiere = 0; //variable pour temporisation lumiere OFF

//**********************************DEBUT du debugtest**********************************
volatile unsigned long debugtime = 0;
volatile unsigned int debugnum = 0;
volatile byte laststate = 0;
volatile unsigned int debugwait = 0;
//********************* debug("NOM A AFFICHER") *******************
//Retourne sur le moniteur serie, le numero de "state" correspondant à l'état actuel de l'alarme
//Et le temps entre deux appel de debug(" ") en µS et mS, pour mesuré la durée d'un tour dans une boucle
// un debounce est fait sur 50 appel de debug pour ne pas saturé le port série
void debug(char * c) {
  if (state != laststate) {
    laststate = state;
    debugnum = 0;
  }
  if (debugnum == 0) {
    debugtime = micros();
    debugnum++;
    Serial.println(c);
  }
  else if (debugnum == 1) {
    debugtime = micros() - debugtime;
    debugnum++;
    Serial.print("State = "); Serial.println(state);
    Serial.print(debugtime); Serial.println("µS");
    Serial.print(debugtime / 1000); Serial.println("mS");
    laststate = state;
    debugwait = 25;
    if (state == 0 or state == 1) debugwait = 2000;
  } else if (debugnum < debugwait) debugnum++;
  else if (debugnum == debugwait) debugnum = 0;
}
//*********************************FIN du DEBUG TEST ****************************

void initpin() {
  pinMode(buzzer, OUTPUT);
  pinMode(ledVerte, OUTPUT);
  pinMode(ledRouge, OUTPUT);
  pinMode(alarme, OUTPUT);
  pinMode(sirene, OUTPUT);
  pinMode(lumiere, OUTPUT);

  digitalWrite(lumiere, HIGH); //attention commande inversé pour la platine relais
  digitalWrite(alarme, HIGH); // idem

  digitalWrite(ledVerte, LOW);
  digitalWrite(ledRouge, LOW);
  digitalWrite(sirene, HIGH);//commande inversé (test Only)
  digitalWrite(buzzer, LOW);

  pinMode(RAZ, INPUT_PULLUP);
  pinMode(contact, INPUT_PULLUP);
}
void setup() {
  initpin();

  lcd.init();                      // initialize the lcd
  lcd.backlight();
  Serial.begin(9600);         // Initialize serial communications with the PC
  rfidserial.begin(9600); // the RDM6300 runs at 9600bps
  tone(buzzer, 220, 1000);
  // EEPROM_reset() ;
  affromdebug();
  affclef();
  attachInterrupt(1, pcint1, CHANGE); //interuption N°1 donc broche 3 = contact
  interrupts();
  if (digitalRead (2) == LOW) {
    delay(500);
    EEPROM_reset();
    delay(500);
  }//fin du if RAZ (debug test only)
}

void pcint1() { //interruption détection ouverture porte
  delayMicroseconds(32000); //0.0032sec soit 32ms
  delayMicroseconds(32000); //0.0032sec soit 32ms
  delayMicroseconds(32000); //0.0032sec soit 32ms
  if (digitalRead(contact)) {// porte ouverte
    if (state == 1) state = 2; //si alarme activé (state1) alors minuterie alarme déclenché (state2)
    if (state == 5) state = 6; //si porte ouverte après reboot alors on lance la minuterie de rappel.
    if (state == 7) state = 11; //si porte reouverte après fermeture
  }
  else { //porte fermer
    if (state == 0) state = 4;//si alarme désactivé alors on déclenche le minuteur d'armement
    if (state == 5) state = 4;// si porte fermer après reboot (coupure de courant) on déclenche le minuteur d'armement
    if (state == 9) state = 4; //si porte ouverte puis fermé après reboot
    if (state == 10) state = 4; //si porte refermer après state 10 alors retour au state 7
  }
}

void loop() {


  lecturerfid();
  gestion();
}

void gestion() {
  if (etat == 2) {
    state = 0;
    digitalWrite(alarme, HIGH); //alarme OFF
    lcdaff("-ALARME-", 65531);
    digitalWrite(sirene, HIGH); //test ONLY
  } etat = 0;

  switch (state) {
    case 0:  // alarme désactivé
#ifdef debugcode
      debug("DEBUG state 0");
#endif
      lumiereON();
      greenled(125, 250);
      if (millis() - prevmillis > minuteur) {
        minuteur = 5000;
        prevmillis = millis();
        lcdaff("-ALARME-", 65531);
      }
      break;

    case 1: //alarme armé  (depuis state 3 ou state 7 ou  state 10)
#ifdef debugcode
      debug("DEBUG state 1");
#endif
      redled(250, 5000); // led rouge blink 100ms ON  5000ms OFF
      if (millis() - prevmillis > 2000) { //interroge l'état du capteur toutes les deux secondes
        prevmillis = millis();
        pcint1();//interrogation manuel car l'interruption ce déclenche seulement sur un changement d'état.
        lcdaff("-ALARME-", 65530);
      }
      lumiereOFF();
      digitalWrite(sirene, HIGH); //test ONLY
      break;

    case 2: // alarme déclenché , minuteure avant sirene (depuis pcint1 armement alarme automatique)
#ifdef debugcode
      debug("DEBUG state 2");
#endif
      lumiereON();
      minuteur = 15000; //minuteur de 15sec
      prevmillis = millis();
      tempoled = 0 ; //correction bug redled(1,5000); de state1
      state = 8;
      break;

    case 3: //alarme déclanché , sirène!!! (depuis state 8)
#ifdef debugcode
      debug("DEBUG state 3");
#endif
      lecturerfid();
      if (millis() - prevmillis > minuteur) {
        lcdaff("-ALARME-", 65530);
        state = 1;// temps écoulé armement de l'alarme
        digitalWrite(alarme, HIGH); //Eteind la sirene maison
        break;
      }
      alarmeext(500, 2000); // 1000 2000 déclenchement relais alarme maison  alarmeext( un'int ON, un'int OFF) en mSec
      lcdaff("!SIRENE!", ((minuteur - (millis() - prevmillis)) / 1000));
      sireneON();
      break;

    case 4://alarme en attente d'armement ( minuteur )
#ifdef debugcode
      debug("DEBUG state 4");
#endif
      digitalWrite(alarme, HIGH); //alarme OFF
      minuteur = 20000; // minuteur de 20sec
      prevmillis = millis();
      state = 7;
      break;

    case 5: // alarme en attente ( reboot ???)
#ifdef debugcode
      debug("DEBUG state 5");
#endif
      digitalWrite(sirene, HIGH); //test ONLY
      pcint1();
      break;

    case 6: // alarme rebooté porte ouverte !!! (depuis pcint1 depuis state5)
#ifdef debugcode
      debug("DEBUG state 6");
#endif
      minuteur = 50000; // minuteur de 50sec
      prevmillis = millis();
      lumiereON();
      state = 9;
      break;

    case 7: //minuteur attente armement (depuis state 4)
#ifdef debugcode
      debug("DEBUG state 7");
#endif
      if (millis() - prevmillis > minuteur) {
        noTone(buzzer);
        tone(buzzer, 1320, 2000); // temps écoulé beep armement de l'alarme
        lcdaff("-ALARME-", 65530);
        state = 1;// temps écoulé armement de l'alarme
        tempoled = 0; //correction bug
        break;
      }
      lcdaff("!!WAIT!!", ((minuteur - (millis() - prevmillis)) / 100));
      beep(880, 20, 500);//beep freq,durée ms, pause ms
      redgreen(500, 500);
      break;

    case 8: //minuteur attente sirene (depuis state2) ( ou state 9)
#ifdef debugcode
      debug("DEBUG state 8");
#endif
      if (millis() - prevmillis > minuteur) {
        state = 3; //temps écoulé sirene
        minuteur = 60000;//sirene pendant 60sec
        prevmillis = millis();
        digitalWrite(ledRouge, HIGH); //led rouge ON
        digitalWrite(alarme, HIGH); //Eteind la sirene maison
        break;
      }
      if ((millis() - prevmillis > 10000)) alarmeext(50, 2000); // déclenchement relais alarme maison  alarmeext( byte ON, byte OFF) en mS
      lcdaff("!ALARME!", ((minuteur - (millis() - prevmillis)) / 100));
      beep(1760, 20, 500);
      redled(50, 500); // led rouge blink 50ms ON  500ms OFF
      break;

    case 9: // minuteur attente armement reboot (depuis state6)
#ifdef debugcode
      debug("DEBUG state 9");
#endif
      if (millis() - prevmillis > minuteur) {
        noTone(buzzer);
        tone(buzzer, 1320, 2000); // temps écoulé beep armement de l'alarme
        state = 8;// temps écoulé armement de l'alarme
        break;
      }
      lcdaff("!REBOOT!", ((minuteur - (millis() - prevmillis)) / 100));
      redgreen(250, 250);; // led rouge vert blink 100ms rouge  100ms verte
      beep(3960, 50, 250);
      break;

    case 10: //porte reouverte pendant delai d'attente armement (depuis state11)
#ifdef debugcode
      debug("DEBUG state 10");
#endif
      if (millis() - prevmillis > minuteur) {
        noTone(buzzer);
        tone(buzzer, 1320, 2000); // temps écoulé beep armement de l'alarme
        lcdaff("-ALARME-", 65530);
        state = 1;// temps écoulé armement de l'alarme
        break;
      }
      lcdaff("DANGER", ((minuteur - (millis() - prevmillis)) / 100));
      beep (3960, 50, 250);
      redgreen(100, 100);
      break;

    case 11: //porte reouverte pendant delai d'attente armement (depuis pcint1 et state7)
#ifdef debugcode
      debug("DEBUG state 11");
#endif
      lumiereON();
      minuteur += 50000;
      state = 10;
      break;
  }
}

void lumiereON() {
  if (digitalRead(lumiere)) digitalWrite(lumiere, LOW); //Allume la lumiere
}

void lumiereOFF() { // Eteind la lumiere si allumé avec une tempo de 9sec env, attention appel en boucle de la fonction.
  if (!digitalRead(lumiere) &&  (millis() - tempolumiere > 15000)) tempolumiere = millis();
  if (!digitalRead(lumiere) && (millis() - tempolumiere > 14000)) digitalWrite(lumiere, HIGH);
}


void lcdaff(char *A, unsigned int B) {
  lcd.home();
  lcd.print(A);
  lcd.print(F("        "));
  lcd.setCursor(0, 1);
  lcd.print(F("  "));
  if (B == 65530) lcd.print(F("ON"));
  else if (B == 65531) lcd.print(F("OFF"));
  else lcd.print(B);
  lcd.print(F("        "));
}

void beep( int f, int t, int tt) {
  if (millis() - tempobeep > tt) {
    tone (buzzer, f, t);
    tempobeep = millis();
  }
}

void alarmeext (unsigned int a, unsigned int b) {
  if (millis() - tempoext > (a + b)) {
    digitalWrite(alarme, LOW);
    tempoext = millis();
  }
  else if ((millis() - tempoext > a)) {
    digitalWrite(alarme, HIGH);
  }
}

void redled(unsigned int u, unsigned int i) {
  if (millis() - tempoled > (i + u)) {
    digitalWrite(ledVerte, LOW);
    digitalWrite(ledRouge, HIGH);
    tempoled = millis();
  }
  else if (millis() - tempoled > u) digitalWrite(ledRouge, LOW);
}

void redgreen(unsigned int u, unsigned int i) {
  if (millis() - tempoled > (i + u)) {
    digitalWrite(ledVerte, LOW);
    digitalWrite(ledRouge, HIGH);
    tempoled = millis();
  }
  if ((millis() - tempoled > u)) {
    digitalWrite(ledRouge, LOW);
    digitalWrite(ledVerte, HIGH);
  }
}

void greenled(unsigned int u, unsigned int i) {
  if (millis() - tempoled > (u + i)) {
    digitalWrite(ledRouge, LOW);
    digitalWrite(ledVerte, HIGH);
    tempoled = millis();
  }
  else if ((millis() - tempoled > u)) digitalWrite(ledVerte, LOW);
}

void sireneON() {
  int f = 1045;
  noTone(buzzer);
  do {
    tone (sirene, f);
    if (micros() - temposirene > 1000) {
      f += 55;
      temposirene = (micros());
    }
    if (etat == 2) {
      noTone(sirene);
      break;
    }
  } while (f < 6600);
  /*lecturerfid();
    do {
    tone (sirene, f);
    if (temposirene < micros()) {
      f -= 55;
      temposirene = (micros() + 1000);
    }
    if (etat == 2) {
      noTone(sirene);
      break;
    }
    } while (f > 1045);
  */ noTone(sirene);
  noTone(buzzer);
  tone(buzzer, 3960, 50);
}

void lecturerfid() {
  lirerfid();
  if (taglu) {
    if (whatrfid() > 0) {
      Serial.println(F("TAG LU OK"));
      lcd.clear();
      lcd.print(F("!TAG OK!"));
      lcd.setCursor(0, 1);
      lcd.print(F("!TAG OK!"));
      digitalWrite(ledRouge, LOW);
      digitalWrite(ledVerte, HIGH);
      delay(500);
    }
    else {
      Serial.println(F("TAG LU INCONNU!!!"));
      lcd.clear();
      lcd.print(F("!TAG KO!"));
      lcd.setCursor(0, 1);
      lcd.print(F("!TAG KO!"));
      digitalWrite(ledVerte, LOW);
      digitalWrite(ledRouge, HIGH);
      delay(500);
    }

  }// if taglu
}



//   lirerfid(); //on lis la clef   le résultat de la lecture sera disponible dans le tableau rfid[16]
void lirerfid() {

  taglu = false;
  memset(rfid, 0, sizeof(rfid)); //erase tagNumber

  if (millis() - timerfidserial > 1000) {
    if (endrfidserial)
    {
      rfidserial.begin(9600); // the RDM6300 runs at 9600bps
      endrfidserial = false;
    }
    while ( rfidserial.available()) {
      int BytesRead = rfidserial.readBytesUntil(3, rfid, 15);//EOT (3) is the last character in tag
      affiche(rfid, sizeof(rfid));
      if (rfid[0] == 0x02 && rfid[1] > 2 && rfid[12] > 0x30 && rfid[13] == 0)
      {
        taglu = true; //verifie que le tag lu peut etre valide
        timerfidserial = millis();
        endrfidserial = true;
        rfidserial.end();
      }
      else {
        Serial.println(F("PAS DE TAG LU OU ERREUR DE LECTURE"));
        // lcd.clear();
        //  lcd.print(F("ERREUR  "));
      }
    }//fin while
  }

}//fin de lirerfid


// ecrireblock (addr de début du block, tableau de valeur, taille du tableau)
void ecrireblock (int adressestart, byte *buffer, byte bufferSize) {
  for (byte index = adressestart; index < (adressestart + bufferSize); index++) {
    EEPROM.update(index, buffer[(index - adressestart)]);
  }//fin du for

}//fin de ecrireblock

//Permet la lecture d'un block mémoire (un bloque est composé de N octets)
void lireblock (int addrBlock, byte blockSize) {
  int x = 0;
  for (int index = addrBlock; index < (addrBlock + blockSize); index++) {
    rom[x] = EEPROM.read(index);
    x++;
  }// fin for
}//fin de lireblock    résultat de lecture dans le tableau rom

// addkey charge dans la variable 'addkey' l'adresse du 1er octet de la key n en fonction de sa taille
unsigned int addkey (byte numkey, byte sizekey) {
  if (numkey == 0) return 0; // retourne 0 si l'adresse est invalide (clef 0) soit inconnu
  unsigned int y = ( sizekey * (numkey - 1));
  y = y + 2;
  return y;
}//fin de addkey

byte nbkey () {
  return EEPROM.read (1);
}//fin de nbkey

void newkey () { //eregistre la key dans l'EEPROM
  unsigned int index = EEPROM.read(1); //récupère le nombre de key enregistré
  index++;
  ecrireblock(addkey(index, taillekey), rfid, taillekey); // copie la key rfid lu en EEPROM
  EEPROM.update(1, (EEPROM.read(1) + 1)); //Met à jour le nombre de key enregistrée
}//fin newkey

// verifie si la key (rfid) lue est connu, retourne l'addresse du 1er octet de son emplacement dans l'EEPROM .
byte knowkey () {
  if (nbkey() == 0 ) return 0; // si aucune clef enregistré on retounrne 0
  for ( byte y = 0; y < nbkey(); y++) {  // on teste les "n" clef enregistrées
    byte count = 0; //initialisation du compteur d'octets valides
    lireblock (addkey(y + 1, taillekey), taillekey); //on lit la clef n et on la copie dans rom
    for ( byte x = 0; x < taillekey; x++) { // on boucle pour comparer les octets de rom et rfid
      if (rom[x] != rfid[x]) break; //si un octet est différent on sort de la boucle ( et on passe a la clef suivante)
      count++; //si la comparaison est OK on incrémente le compteur
    }
    if (count == taillekey) return y + 1; //si le compteur arrive au nombre d'octets d'une clef alors OK (on donne son adresse rom)
  }
  return 0; //sinon on ne la connais pas on retourne 0

}// fin de knowkey

void ajouterkey() { // ajouter une nouvelle clef (appel "newkey" pour enregistrement dans l'EEPROM)
  byte master = 1;
  if (etat == 1) timerajouterkey = millis(); // tempo pour l'attente de nouvelle clef à enregistré
  Serial.println(F("ATTENTE NEW KEY"));
  //lcd.clear();
  //lcd.print(F("ADD KEY "));
  while (master == 1) {
    lirerfid();
    lcd.setCursor(0, 1);
    //lcd.print((5000 - ( millis() - timerajouterkey)) / 100);
    lcdaff("ADD KEY", (5000 - ( millis() - timerajouterkey)) / 100);
    //lcd.print(F("  "));
    if (taglu) {
      if ((knowkey()) == 0) {
        newkey();
        tone (buzzer, 2349, 500);
        master = 0;
        //lcd.setCursor(0, 1);
        //lcd.print(F("ADD: "));
        //lcd.print(nbkey());
        lcdaff("ADD :", nbkey());
        Serial.println(F("NOUVELLE KEY"));
        Serial.print(F(" N"));
        Serial.println(nbkey());
        delay(1000);
        etat = 2;
      }//fin if knowkey
    }//fin if taglu
    if (millis() - timerajouterkey > 5000) {
      tone(buzzer, 1760, 500);
      master = 0;
      etat = 2;
      lcd.clear();
      lcd.print(F("CASE 2"));
      lcd.setCursor(0, 1);
      lcd.print(F("MASTER"));
      Serial.println(F("CASE 2 BIS --- MASTER KEY UNLOCK"));
      delay(1000);
    }//fin if timerajouterkey
  }//fin while
}//fin de ajouterkey

//whatrfid();//lis la clef et retourne une valeur 0 si clef inconnu 1 si master et 2 si connu (résultat "etat")
byte whatrfid() {
  switch (knowkey()) {
    case 0:
      etat = 0;
      Serial.println(F("CASE 0 WAIT----"));
      //si aucune clef enregistré alors on crée la master key
      if (nbkey() == 0) { // la 1er clef enregistré sera la clef master !
        Serial.println(F("CASE 0 - ENR MASTER KEY"));
        newkey();
        etat = 2;
        lcd.clear();
        lcd.print(F("CASE 0"));
        lcd.setCursor(0, 1);
        lcd.print(F("MASTER"));
        delay(1000);
      }
      else Serial.println(F("CASE 0 - KEY INCONNU WTF!!!!"));
      memset(rfid, 0, sizeof(rfid)); //erase tagNumber
      tone(buzzer, 4000, 500); //sortie buzzer clef refuser 880
      return 0;
    //  break;

    case 1:
      etat = 1;
      Serial.println(F("CASE 1 WAIT----"));
      //   lcd.clear();
      //   lcd.print(F("CASE 1  "));
      tone(buzzer, 1975, 500); //sortie buzzer master key
      delay(500);////////////////////////////////////////////////////
      ajouterkey();
      return 2;
    //break;

    default:
      Serial.println(F("CASE default WAIT----"));
      //   lcd.clear();
      //  lcd.print(F("CASE DEF"));
      tone(buzzer, 1760, 500); //sortie buzzer clef valide
      etat = 2;
      return 2;
      // break;
  }//fin switch
  // addrkey = addkey(knowkey(), taillekey); //on charge dans addrkey l'adress EEPROM du 1er octet de la clef (si connu)
  // return knowkey();//On retoune l'état (0 = inconnu , 1 = master key , 2 et + = clef connu N°)
}//fin de whatrfid



// efface la totalité de l'EEPROM
void EEPROM_reset() {
  tone(buzzer, 2640, 500); //sortie buzzer start RAZ EEprom
  Serial.print(F("RAZ 0%"));
  for (int i = 0; i <= EEPROM.length(); i++) {
    EEPROM.update(i, 0 );
    if (i == (EEPROM.length() / 4)) {
      Serial.print(F(" / 25%"));
    }
    if (i == (EEPROM.length() / 2)) {
      Serial.print(F(" / 50%"));
    }
    if (i == (EEPROM.length() - (EEPROM.length() / 4))) {
      Serial.print(F(" / 75%"));
    }
  }
  Serial.println(F(" / 100%"));
  tone(buzzer, 2200, 1000); //sortie buzzer Fin RAZ EEprom
} // fin de EEPROM_I2C_reset
void affiche(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}//fin de affiche

void affromdebug() {
  Serial.print(F("Nombre de clef connu = ")); Serial.println(nbkey());
  // lcd.print(F("KEY: "));
  // lcd.print(nbkey());
  for (byte F = 0; F < nbkey(); F++) {
    lireblock(addkey(F + 1, taillekey), taillekey);
    Serial.print(F("KEY N = ")); Serial.println(F + 1);
    affiche (rom, taillekey);
  }
}
void affclef() {
  for ( byte y = 0; y < nbkey(); y++) {  // on teste les "n" clef enregistrées
    Serial.print(F("Key: "));
    Serial.print(y);
    Serial.print(F(" Data= "));
    lireblock (addkey(y + 1, taillekey), taillekey); //on lit la clef n et on la copie dans rom
    for ( byte i = 0; i < taillekey; i++) {
      Serial.print(rom[i] < 0x10 ? "0" : "");
      Serial.print(rom[i], HEX);
      Serial.print(" ");
    }// for i (n byte)
    Serial.println();
  }// for y (n clef)
}// fin affclef


