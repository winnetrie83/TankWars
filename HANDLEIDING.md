# TankWars 0.3.3 — Qt 6 / C++17

## Installatie

1. Pak de volledige ZIP uit; behoud de mappen `src/` en `assets/` naast `CMakeLists.txt`.
2. Open **CMakeLists.txt** in Qt Creator.
3. Selecteer **Qt 6.12 MinGW 64-bit** (of een andere Qt 6.4+ Widgets/Network-kit).
4. Build via **Build > Rebuild All**. Gebruik een lege buildmap bij problemen na een vorige versie.
5. Voor de volledige geluidsondersteuning: installeer ook de Qt Multimedia-component in de gekozen kit. Zonder die component werkt onder Windows de fallback via `winmm` (zonder volumeregeling), op overige platformen zonder Multimedia zonder audio.
6. Start voor een LAN-game op één computer **Host**, op de andere **Join** met IP-adres van de host en dezelfde TCP-poort (standaard 45454). Laat de poort toe in de firewall.

**Multiplayercompatibiliteit:** alle deelnemers moeten 0.3.3 gebruiken. De protocolversie is **4** (nieuwe muurbouw-, respawn- en projectielcommando’s). De JSON-mapversie is gelijk gebleven (versie 1), zodat eerdere opgeslagen kaarten geladen kunnen worden.

## Lobby

Kies je team en **Jeep**, **Tank** of **Missile launcher** met het voertuigkeuzemenu. De host kan teams (2/4), capturetijd, resources, respawnprijs, bots en een kaart instellen. Iedereen moet **READY** zijn voordat de host kan starten. Na vijf seconden aftellen verspreidt de host de kaart. Het gevecht begint pas nadat de verbonden menselijke spelers klaar zijn met laden.

## Besturing

| Actie | Standaard |
| --- | --- |
| Boven / links / onder / rechts | **Z / Q / S / D** |
| Richten | Muis bewegen |
| Schieten | Linkermuisknop vasthouden of spatie |
| Selecteer object voor upgrades | **Rechtermuisknop** op eigen tank, base, resource node of torenplatform |
| Wissel naar upgrades van eigen voertuig | Knop rechts **SELECT MY VEHICLE** |
| Leg een mijn (alleen Jeep) | **M** (3 Oil + 2 Iron + 2 Tech per mijn; maximaal 8 actieve mijnen) |
| Plaats stenen muur | **B** of knop **BUILD STONE WALL** rechts: daarna **linkermuisklik** op gekozen tegel (12 Stone); rechtsklik / Escape annuleert |
| Zoom | Muiswiel |

Toetsen zijn individueel te wijzigen in **Controls & Audio Settings**. Via de rechterkolom krijg je bij een selectie relevante aankopen, resourcekosten en expliciete redenen waarom een aankoop niet kan. Aankopen worden op de host gevalideerd, niet alleen op de client.

### Voertuigen

| Voertuig | Basis-HP | Verplaatsingssnelheid | Schade t.o.v. tank | Bijzonder |
| --- | ---: | ---: | ---: | --- |
| Jeep | 100 | 161 | 0,5× | **Light armor**, snel machinegeweer, 2× veroveringssnelheid en mijnen |
| Tank | 200 | 120 | 1× | **Heavy armor**, zwaar kanon, 2× basis-HP |
| Missile launcher | 100 | 132 | 2× | **Light armor**, tragere raketten met explosieschade |

Je kunt **bij je eigen levende base** van voertuig wisselen via je persoonlijke upgrades. Armor-, gun-, speed-, radar- en vuursnelheidslevels blijven aan de speler verbonden. Het percentage van je huidige HP blijft bij het wisselen behouden. Armor is onderverdeeld in **Light, Medium, Heavy**. Geen voertuig heeft momenteel Medium armor; die klasse is voorbereid voor latere types. Mijnschade per categorie: **Light 125**, **Medium 103** (nog niet gebruikt), **Heavy 85** HP. Een onverbeterde Jeep of Missile launcher (100 HP) wordt door één mijn vernietigd, maar na een persoonlijke armorupgrade (132 HP) kan het voertuig één mijn bij volledige gezondheid overleven. De Tank (200 HP) is beter bestand tegen mijnen. Het persoonlijke upgradelevel verandert de armorcategorie niet.

Mijnen worden per plaatsing betaald uit de gedeelde teamvoorraad: **3 Oil + 2 Iron + 2 Tech**. Bij onvoldoende resources toont het spel de ontbrekende materialen. De mijnen worden **alleen naar de eigen teamclients verzonden**.

### Fog of war en radar

- **Onbekend:** zwart.
- **Eerder verkend, niet actueel zichtbaar:** lichte grijze sluier (veel transparanter dan in 0.2.1), geen actuele informatie over vijanden.
- **Actueel zichtbaar:** normale weergave; teamgenoten delen het zicht.
- **Base radar:** start op 8 tegels; per upgrade +3 tegels.
- **Player radar:** start op 9 tegels; per upgrade +3 tegels.
- **Tower range/radar:** start op 7 tegels zicht; per upgrade +2 tegels en groter vuurbereik.

De host filtert tegenstanders, projectielen, muren en torens volgens het zicht van ieder team. Mijnen zijn uitsluitend voor het eigen team beschikbaar. Eigen spelers en structuren zijn bekend aan de client, maar worden door de client niet buiten actueel zicht getekend.

### Resourcepunten en torenplatformen

Resourcepunten zijn grafisch 2× zo groot en vereisen 2× de ingestelde veroveringstijd; een Jeep telt dubbel als veroveraar. Bij eigendom leveren resourcepunten elke 2 seconden 2 stuks van de bijbehorende grondstof.

Torenplatformen vlak bij een base (maximaal 6 tegels) beginnen in handen van dat team. De standaardmap heeft per base vier van zulke platformen. Custom-mapmakers bepalen de startverdeling via de positie van de platforms tegenover de bases. Verder gelegen platforms moeten worden ingenomen door een voertuig. Zolang er een levende toren op het platform staat, is herovering **niet mogelijk**. Vernietig eerst de toren en verover daarna het lege platform. Een toren kan alleen gebouwd worden op een eigen leeg platform.

### Torentypes

| Type | Basisvuurinterval | Basisschade | Basisbereik |
| --- | ---: | ---: | ---: |
| Minigun | 0,22 s | 14 | 250 |
| Cannon | 1,05 s | 42 | 280 |
| Rocket | 1,65 s | 65 | 320 |

De Minigun vuurt kleine snelle kogels met een hoog ratelend geluid, de Cannon vuurt zware shells, en de Rocket lanceert trage zichtbare raketten die bij inslag een explosie met **gebiedsschade** veroorzaken. De drie modellen hebben verschillende kanonopbouwen, draaien naar het doel en geven een mondingsvlam bij het schieten.

Iedere toren heeft eigen upgrades voor **fire rate**, **damage**, **armor** en **range/radar**, elk maximaal level 4. Torens en torenplatformen zijn twee keer zo groot als voorheen; nieuwe torens hebben 280 HP.

### Bases, muren en respawn

Bases beginnen met **1200 HP**, zijn grafisch dubbel zo groot en produceren om de **5 seconden +1 Oil en +1 Iron** zolang ze leven. Respawn vereist de in de lobby ingestelde hoeveelheid van zowel Oil als Iron en minstens 10 seconden wachten. **Na elke dood verschijnt een keuzepaneel in de rechterkolom**. Kies daar Jeep / Tank / Missile launcher en klik op **CONFIRM RESPAWN VEHICLE**. Je kunt de keuze nog veranderen zolang je dood bent. De server spawnt menselijke spelers pas als er een voertuig is gekozen, de 10 seconden om zijn én er genoeg Oil/Iron beschikbaar is. Bots kiezen automatisch hun eigen voertuig. Als je omkomt, raak je nog steeds het laatst aangekochte persoonlijke upgradelevel kwijt. Base armor en radar zijn afzonderlijke aankopen.

Stenen muren kosten **12 Stone**, hebben **400 HP** en worden door projectielen beschadigd. Activeer de bouwmodus via de knop **BUILD STONE WALL** (rechterkolom) of via **B**. Beweeg de muis: je ziet een groene/rode tegelindicatie. Klik **links** op de doeltegel om een bouwverzoek te sturen; **rechtsklik** of **Escape** annuleert. Muren kunnen binnen vijf tegels afstand op een lege grastegel geplaatst worden. De host weigert plaatsing op obstakels, bij ingangen en veroveringspunten, op voertuigen, of wanneer hierdoor een objectief onbereikbaar zou worden. Het statusvak rechts meldt waarom een plaatsing mislukt.

### Geluid

Het schotgeluid wordt nu geactiveerd door de oplopende **server-bevestigde schotteller** van de eigen speler, niet meer door een onafhankelijke clienttimer terwijl de vuurtherhaling wordt ingedrukt. Omdat alle waarnemingen via netwerkframes van ca. 66 ms lopen, blijft er een kleine netwerkvertraging mogelijk. Zichtbare schoten van nabije medespelers, tegenstanders en torens krijgen nu ook passende geluidseffecten; die worden alleen afgespeeld als de host schoten heeft geregistreerd. Voor Jeep MG, Minigun tower, Cannon, Rocket, raketexplosie en mijnexplosie zijn aparte WAV-geluiden meegeleverd. De geluiden zijn synthetisch gegenereerd en vereisen geen externe bestanden. Treffergeluiden, verovering en explosies blijven apart bestaan.

## Mapeditor

Open de ingebouwde mapeditor vanaf het beginscherm of in de hostlobby. Bestaande JSON-kaarten gebruiken nog steeds hetzelfde formaat. Bij een nieuwe aangepaste map zijn de posities van de torenplatformen bepalend voor welke platforms bij de start al aan een base toebehoren. Controleer bij oudere kaarten of de grotere gebouwen elkaar visueel niet overlappen. De editor valideert voor gekozen teamconfiguraties de bereikbaarheid van objectieven.

## Controle na build

1. **Host + een bot:** beweeg met ZQSD, vuurgeluid uitsluitend als de host werkelijk schiet, kaart verschuift, fog toont ontdekte gebieden.
2. **Radar:** koop Player radar vlak bij de eigen base; selecteer eigen base met rechtermuisknop voor Base radar. Het zicht moet zichtbaar toenemen.
3. **Torenplatform:** probeer een niet-veroverd pad te bouwen; de kosten én reden worden getoond. Verover het pad, bouw elk van de drie types, test verschillende schotsnelheden, geluiden, raketspatten en upgrade een toren.
4. **Voertuigen:** test Jeep MG, Tank shells en Missile rockets; leg een Jeep-mijn (3 Oil / 2 Iron / 2 Tech). Controleer dat één mijn een onverbeterde Light-armor-Jeep vernietigt en een full-health Heavy Tank niet.
5. **Muren:** activeer B/bouwknop, klik een vrije tegel, annuleer met Esc en probeer een ongeldige locatie. Controleer feedback en destructie door kanonschoten/raketten.
6. **Respawn:** sterf, kies Jeep/Tank/ML, bevestig en controleer de timer, kostprijs en behoud van universele upgrades.
7. **Netwerk met twee pc's:** controleer ready, automatische mapoverdracht, fog op beide teams, bouwcommando, respawnkeuze en mijngeheimhouding.

**Bouwstatus:** deze broncode is voorbereid voor Qt 6.12 / MinGW 64-bit, maar niet daadwerkelijk gecompileerd in de omgeving waarin het archief is gemaakt (daar is de Qt 6 SDK niet aanwezig). De eerste lokale Qt Creator-build blijft dus noodzakelijk.
