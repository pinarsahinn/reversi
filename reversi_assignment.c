#include <stdio.h>   // printf, fopen, fread, fwrite gibi giriş/çıkış işlemleri
#include <stdlib.h>  // atoi, exit
#include <string.h>  // strcmp, strlen, strncpy, memset
#include <time.h>    // time(NULL) ile benzersiz game_id üretmek için
#include <ctype.h>   // tolower, toupper, isdigit, isalpha

/* Reversi board size / Reversi tahta boyutu */
#define BOARD_SIZE 8          // Tahta 8x8
#define USERNAME_LEN 50       // Kullanıcı adı en fazla 49 karakter + '\0'
#define PIN_LEN 16            // PIN girişi için ayrılan dizi boyutu
#define ACCOUNTS_FILE "accounts.dat"      // Kullanıcı hesaplarının tutulduğu dosya
#define SAVES_FILE "saved_games.dat"      // Kayıtlı oyunların tutulduğu dosya

/* Board cell states / Tahta hücre durumları */
enum Cell {
    EMPTY = 0,
    BLACK = 1,
    WHITE = 2
};

/* Stores account information / Kullanıcı hesap bilgilerini tutar
   PIN burada düz olarak değil, hashlenmiş halde tutulur.
   Bu, hocanın özellikle istediği güvenlik kısmıdır. */
typedef struct {
    char username[USERNAME_LEN];
    unsigned long pin_hash;
    int games_won;
    int games_lost;
} UserRecord;   // UserRecord adında bir veri tipi oluşturur

/* Stores one saved game state / Bir kayıtlı oyun durumunu tutar */
typedef struct {
    int game_id;                             // Bu save'e özel kimlik
    char username[USERNAME_LEN];             // Kaydın sahibi
    int board[BOARD_SIZE][BOARD_SIZE];       // 8x8 oyun tahtası
    int current_turn;                        // Sıra kimde
    unsigned long checksum;                  // Dosyada oynama yapıldı mı kontrolü
} GameState;

/* Simple coordinate pair / Basit satır-sütun çifti
   Hangi taşların çevrileceğini geçici olarak saklamak için kullanılır. */
typedef struct {
    int row;
    int col;
} Position;

/* Hashes strings for PINs and save integrity / PIN ve kayıt bütünlüğü için hash üretir
   Bu fonksiyon string’den hash üretir.

   İki yerde kullanılır:
   1. Kullanıcının PIN’ini hashlemek için
   2. Save dosyasının checksum değerini üretmek için

   Mantık:
   String’i karakter karakter dolaşır ve her karaktere göre hash değerini günceller. */
unsigned long djb2(const char *str) {
    unsigned long hash = 5381;
    int c;

    while ((c = *str++) != 0) {
        hash = ((hash << 5) + hash) + (unsigned long)c;
    }

    return hash;
}

/* Giriş tamponunu temizler.
   Kodun büyük kısmında fgets kullanıldığı için çok kritik değildir ama zararlı da değildir. */
void clear_input_buffer(void) {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
    }
}

/* Yardımcı fonksiyon
   fgets ile alınan string’in sonundaki '\n' karakterini siler.

   Örnek:
   "utku\n" -> "utku" */
void trim_newline(char *text) {
    size_t len = strlen(text);
    if (len > 0 && text[len - 1] == '\n') {
        text[len - 1] = '\0';
    }
}

/* Kullanıcıdan güvenli şekilde satır okur.

   Ne yapar?
   - Ekrana prompt basar
   - fgets ile veri alır
   - Satır sonunu siler
   - Başarılıysa 1, başarısızsa 0 döndürür

   Böylece kod tekrarını azaltır. */
int read_line(const char *prompt, char *buffer, size_t size) {
    printf("%s", prompt);
    if (fgets(buffer, (int)size, stdin) == NULL) {
        return 0;
    }
    trim_newline(buffer);
    return 1;
}

/* İki yazıyı büyük-küçük harfe duyarsız karşılaştırır.

   Böylece kullanıcı:
   save / SAVE / Save
   gibi farklı yazsa da komut çalışır. */
int strings_equal_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Finds a user in accounts.dat and optionally returns record + byte position
   accounts.dat içinde kullanıcıyı bulur; istersek kaydı ve byte konumunu döndürür.

   Parametreler:
   - username : Aranacak kullanıcı adı
   - out_user : Kullanıcı bulunduysa bilgilerini buraya yazar
   - out_pos  : Dosyadaki byte konumunu buraya yazar

   out_pos neden önemli?
   Kullanıcı istatistiklerini sonradan güncellemek için dosyada aynı kaydın üstüne
   yazmak gerekir. Bunun için ilgili konumu bilmek gerekir.

   Çalışma mantığı:
   - Dosyayı "rb" modunda açar
   - UserRecord kayıtlarını tek tek fread ile okur
   - strcmp ile username karşılaştırır
   - Bulursa:
     * kullanıcı kaydını istersek kopyalar
     * byte konumunu istersek kopyalar
     * 1 döndürür
   - Bulamazsa 0 döndürür */
int find_user_record(const char *username, UserRecord *out_user, long *out_pos) {
    FILE *file = fopen(ACCOUNTS_FILE, "rb");
    UserRecord user;
    long pos = 0;

    if (file == NULL) {
        return 0;
    }

    while (fread(&user, sizeof(UserRecord), 1, file) == 1) {
        if (strcmp(user.username, username) == 0) {
            if (out_user != NULL) {
                *out_user = user;
            }
            if (out_pos != NULL) {
                *out_pos = pos;
            }
            fclose(file);
            return 1;
        }
        pos += (long)sizeof(UserRecord);
    }

    fclose(file);
    return 0;
}

/* Overwrites one user record in place / Tek bir kullanıcı kaydını yerinde günceller

   Çalışma mantığı:
   - Dosyayı "r+b" modunda açar
   - fseek(file, pos, SEEK_SET) ile ilgili byte konumuna gider
   - fwrite ile yeni kullanıcı kaydını yazar

   Böylece:
   - Tüm dosyayı yeniden yazmaya gerek kalmaz
   - Sadece ilgili kullanıcı kaydı güncellenir */
int update_user_record(const UserRecord *user, long pos) {
    FILE *file = fopen(ACCOUNTS_FILE, "r+b");

    if (file == NULL) {
        return 0;
    }

    if (fseek(file, pos, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    if (fwrite(user, sizeof(UserRecord), 1, file) != 1) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

/* Checks if PIN is exactly 4 digits / PIN tam olarak 4 rakam mı kontrol eder */
int is_pin_valid_format(const char *pin) {
    size_t i;
    if (strlen(pin) != 4) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)pin[i])) {
            return 0;
        }
    }

    return 1;
}

/* Creates a new account and stores hashed PIN / Yeni hesap oluşturur ve hashlenmiş PIN saklar

   Adımlar:
   1. Kullanıcı adını alır
   2. Boş mu kontrol eder
   3. Aynı kullanıcı adı var mı kontrol eder
   4. PIN alır
   5. PIN formatını doğrular
   6. PIN'i hashler
   7. games_won = 0, games_lost = 0 yapar
   8. accounts.dat dosyasına ekler */
void create_account(void) {
    UserRecord user;
    char pin[PIN_LEN];
    FILE *file;

    if (!read_line("New username: ", user.username, sizeof(user.username))) {
        return;
    }

    if (strlen(user.username) == 0) {
        printf("Username cannot be empty.\n");
        return;
    }

    if (find_user_record(user.username, NULL, NULL)) {
        printf("This username already exists.\n");
        return;
    }

    if (!read_line("4-digit PIN: ", pin, sizeof(pin))) {
        return;
    }

    if (!is_pin_valid_format(pin)) {
        printf("PIN must contain exactly 4 digits.\n");
        return;
    }

    user.pin_hash = djb2(pin);
    user.games_won = 0;
    user.games_lost = 0;

    file = fopen(ACCOUNTS_FILE, "ab");   // append binary: binary dosyanın sonuna ekleme yapar
    if (file == NULL) {
        printf("Could not open accounts file.\n");
        return;
    }

    if (fwrite(&user, sizeof(UserRecord), 1, file) != 1) {
        printf("Could not save the account.\n");
    } else {
        printf("Account created. Please log in.\n");
    }

    fclose(file);
}

/* Authenticates user and enforces 3 wrong PIN attempts rule
   Kullanıcı girişini doğrular ve 3 yanlış PIN kuralını uygular

   Adımlar:
   1. Kullanıcı adını alır
   2. find_user_record ile kullanıcıyı bulur
   3. 3 deneme boyunca PIN ister
   4. Girilen PIN’i hashler
   5. Dosyadaki pin_hash ile karşılaştırır
   6. Doğruysa giriş başarılı olur
   7. 3 kez yanlışsa program kapanır */
int login_user(UserRecord *logged_in_user, long *user_pos) {
    char username[USERNAME_LEN];
    char pin[PIN_LEN];
    UserRecord user;
    long pos;
    int attempt;

    if (!read_line("Username: ", username, sizeof(username))) {
        return 0;
    }

    if (!find_user_record(username, &user, &pos)) {
        printf("User not found.\n");
        return 0;
    }

    for (attempt = 1; attempt <= 3; attempt++) {
        if (!read_line("PIN: ", pin, sizeof(pin))) {
            return 0;
        }

        if (djb2(pin) == user.pin_hash) {
            *logged_in_user = user;
            *user_pos = pos;
            printf("Login successful. Welcome, %s.\n", user.username);
            return 1;
        }

        printf("Wrong PIN. Attempt %d of 3.\n", attempt);
    }

    printf("Security warning: 3 invalid PIN attempts. Program will exit.\n");
    exit(0);
}

/* Prints the board with B, W and . symbols / Tahtayı B, W ve . sembolleriyle yazdırır

   Görünüm:
   - Sütunlar: 1 2 3 4 5 6 7 8
   - Satırlar: A B C D E F G H
   - Taşlar:
     * B
     * W
     * . */
void print_board(const int board[BOARD_SIZE][BOARD_SIZE]) {
    int row;
    int col;

    printf("\n  ");
    for (col = 0; col < BOARD_SIZE; col++) {
        printf(" %d", col + 1);
    }
    printf("\n");

    for (row = 0; row < BOARD_SIZE; row++) {
        printf("%c ", 'A' + row);
        for (col = 0; col < BOARD_SIZE; col++) {
            char symbol = '.';
            if (board[row][col] == BLACK) {
                symbol = 'B';
            } else if (board[row][col] == WHITE) {
                symbol = 'W';
            }
            printf(" %c", symbol);
        }
        printf("\n");
    }
    printf("\n");
}

/* Sets up the starting 4 pieces for a new game / Yeni oyun için başlangıçtaki 4 taşı yerleştirir

   Yaptıkları:
   - Tüm struct’ı sıfırlar
   - Tahtayı boş yapar
   - Ortadaki 4 taşı yerleştirir
   - Sırayı BLACK yapar
   - Kullanıcı adını kopyalar
   - time(NULL) ile game_id üretir
   - checksum = 0 yapar

   Başlangıç yerleşimi:
   - [3][3] = WHITE
   - [3][4] = BLACK
   - [4][3] = BLACK
   - [4][4] = WHITE */
void initialize_board(GameState *game, const char *username) {
    int row;
    int col;

    memset(game, 0, sizeof(GameState));
    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            game->board[row][col] = EMPTY;
        }
    }

    game->board[3][3] = WHITE;
    game->board[3][4] = BLACK;
    game->board[4][3] = BLACK;
    game->board[4][4] = WHITE;
    game->current_turn = BLACK;
    strncpy(game->username, username, USERNAME_LEN - 1);
    game->game_id = (int)time(NULL);
    game->checksum = 0;
}

/* Verilen koordinat tahtanın içinde mi kontrol eder */
int is_on_board(int row, int col) {
    return row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE;
}

/* Oyuncunun rakibini döndürür.
   - Oyuncu siyahsa rakip beyaz
   - Oyuncu beyazsa rakip siyah */
int get_opponent(int player) {
    return player == BLACK ? WHITE : BLACK;
}

/* Checks all 8 directions and collects opponent pieces that will flip
   8 yönün tamamını kontrol eder ve dönüşecek rakip taşları toplar

   Görevi:
   Oyuncu belirli bir kareye taş koyarsa hangi rakip taşların çevrileceğini bulur.

   Çalışma mantığı:
   - Önce seçilen kare boş mu kontrol eder
   - 8 yönün tamamını sırayla gezer
   - Her yönde:
     * İlk komşu kare rakip mi diye bakar
     * Rakipse aynı yönde ilerler
     * Rakip taşlar boyunca gider
     * Sonunda kendi taşına ulaşırsa o yöndeki taşlar çevrilecek demektir
   - Bulduğu taşları flips[] dizisine kaydeder
   - Toplam çevrilecek taş sayısını döndürür

   Reversi’de bir hamlenin geçerli olması için:
   Arada rakip taşlar olmalı ve en sonunda kendi taşınla kapanmalıdır. */
int collect_flips(const int board[BOARD_SIZE][BOARD_SIZE], int row, int col, int player, Position flips[], int max_flips) {
    int directions[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1},
        {0, -1},           {0, 1},
        {1, -1},  {1, 0},  {1, 1}
    };
    int total = 0;
    int dir;
    int opponent = get_opponent(player);

    if (!is_on_board(row, col) || board[row][col] != EMPTY) {
        return 0;
    }

    for (dir = 0; dir < 8; dir++) {
        int r = row + directions[dir][0];
        int c = col + directions[dir][1];
        Position temp[BOARD_SIZE];
        int temp_count = 0;

        if (!is_on_board(r, c) || board[r][c] != opponent) {
            continue;
        }

        /* Walk in one direction while opponent pieces continue
           Rakip taşlar devam ettiği sürece aynı yönde ilerler */
        while (is_on_board(r, c) && board[r][c] == opponent) {
            if (temp_count < BOARD_SIZE) {
                temp[temp_count].row = r;
                temp[temp_count].col = c;
            }
            temp_count++;
            r += directions[dir][0];
            c += directions[dir][1];
        }

        if (is_on_board(r, c) && board[r][c] == player) {
            int i;
            for (i = 0; i < temp_count && total < max_flips; i++) {
                flips[total++] = temp[i];
            }
        }
    }

    return total;
}

/* Returns whether a player has at least one legal move
   Oyuncunun en az bir geçerli hamlesi var mı kontrol eder

   Çalışma mantığı:
   - Tahtadaki tüm hücreleri gezer
   - Her hücre için collect_flips(...) > 0 mu diye bakar
   - Bir tane bile varsa 1 döndürür
   - Hiç yoksa 0 döndürür

   Bu fonksiyon:
   - oyun sonu kontrolünde
   - sıra pas geçme mantığında kullanılır */
int has_valid_move(const int board[BOARD_SIZE][BOARD_SIZE], int player) {
    int row;
    int col;
    Position flips[BOARD_SIZE * BOARD_SIZE];

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            if (collect_flips(board, row, col, player, flips, BOARD_SIZE * BOARD_SIZE) > 0) {
                return 1;
            }
        }
    }

    return 0;
}

/* Places the piece and flips all bracketed lines / Taşı yerleştirir ve sıkışan taşları çevirir

   Görevi:
   Hamleyi gerçekten tahtaya uygular.

   Çalışma mantığı:
   1. collect_flips ile çevrilecek taşları bulur
   2. Hiç taş çevrilmiyorsa hamle geçersizdir -> 0
   3. Geçerliyse:
      - yeni taşı yerleştirir
      - listedeki rakip taşları çevirir
   4. Çevrilen taş sayısını döndürür */
int apply_move(int board[BOARD_SIZE][BOARD_SIZE], int row, int col, int player) {
    Position flips[BOARD_SIZE * BOARD_SIZE];
    int flip_count;
    int i;

    flip_count = collect_flips(board, row, col, player, flips, BOARD_SIZE * BOARD_SIZE);
    if (flip_count == 0) {
        return 0;
    }

    board[row][col] = player;
    for (i = 0; i < flip_count; i++) {
        board[flips[i].row][flips[i].col] = player;
    }

    return flip_count;
}

/* Counts black, white and empty cells / Siyah, beyaz ve boş hücreleri sayar

   Tahtadaki:
   - siyah taş sayısını
   - beyaz taş sayısını
   - boş kare sayısını

   hesaplar.

   Kullanım alanları:
   - skoru göstermek
   - oyun bitti mi kontrol etmek
   - kazananı belirlemek */
void count_discs(const int board[BOARD_SIZE][BOARD_SIZE], int *black_count, int *white_count, int *empty_count) {
    int row;
    int col;
    *black_count = 0;
    *white_count = 0;
    *empty_count = 0;

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            if (board[row][col] == BLACK) {
                (*black_count)++;
            } else if (board[row][col] == WHITE) {
                (*white_count)++;
            } else {
                (*empty_count)++;
            }
        }
    }
}

/* Builds a checksum from game content to detect tampering
   Oyun verisinden checksum üretir ve dosya kurcalanmış mı kontrol etmeye yardım eder

   Çalışma mantığı:
   Aşağıdaki alanları string haline getirir:
   - game_id
   - username
   - current_turn
   - tüm board elemanları

   Sonra bunların hepsini djb2 ile hashler.

   Neden gerekli?
   Birisi saved_games.dat dosyasını dışarıdan değiştirirse
   yeni hesaplanan hash ile kaydedilmiş checksum uyuşmaz.
   Böylece hile ya da bozulma tespit edilir.

   Not:
   checksum alanı hash hesabına katılmaz.
   Çünkü checksum kendi kendisini hashlememelidir. */
unsigned long compute_game_checksum(const GameState *game) {
    char buffer[4096];
    size_t offset = 0;
    int row;
    int col;

    offset += (size_t)snprintf(buffer + offset, sizeof(buffer) - offset, "%d|%s|%d|", game->game_id, game->username, game->current_turn);
    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            offset += (size_t)snprintf(buffer + offset, sizeof(buffer) - offset, "%d,", game->board[row][col]);
            if (offset >= sizeof(buffer)) {
                buffer[sizeof(buffer) - 1] = '\0';
                break;
            }
        }
    }

    return djb2(buffer);
}

/* Recomputes and stores current checksum / Güncel checksum değerini yeniden hesaplar

   Bu fonksiyon mevcut oyun durumunun checksum’unu günceller.
   Save işleminden önce çağrılması gerekir. */
void sync_game_checksum(GameState *game) {
    game->checksum = compute_game_checksum(game);
}

/* Save updates an existing gameId; Save As creates a new one
   Save mevcut gameId kaydını günceller; Save As yeni kimlik oluşturur

   Bu fonksiyon hem Save hem Save As mantığını yönetir.

   Eğer save_as_new == 1 ise:
   - yeni game_id üretir
   - checksum günceller
   - dosyayı "ab" modunda açar
   - yeni kayıt olarak sona ekler
   Bu tam anlamıyla "Save As" davranışıdır.

   Eğer save_as_new == 0 ise:
   - checksum güncellenir
   - dosyada aynı game_id aranır
   - bulunursa aynı kaydın üstüne yazılır
   - bulunmazsa sona eklenir
   Bu da normal "Save" davranışıdır. */
int save_game_record(GameState *game, int save_as_new) {
    FILE *file;
    GameState stored;
    long pos = 0;

    if (save_as_new) {
        game->game_id = (int)time(NULL);
    }
    sync_game_checksum(game);

    if (save_as_new) {
        file = fopen(SAVES_FILE, "ab");
        if (file == NULL) {
            return 0;
        }
        if (fwrite(game, sizeof(GameState), 1, file) != 1) {
            fclose(file);
            return 0;
        }
        fclose(file);
        return 1;
    }

    file = fopen(SAVES_FILE, "r+b");
    if (file == NULL) {
        file = fopen(SAVES_FILE, "w+b");
        if (file == NULL) {
            return 0;
        }
    }

    while (fread(&stored, sizeof(GameState), 1, file) == 1) {
        if (stored.game_id == game->game_id) {
            if (fseek(file, pos, SEEK_SET) != 0) {
                fclose(file);
                return 0;
            }
            if (fwrite(game, sizeof(GameState), 1, file) != 1) {
                fclose(file);
                return 0;
            }
            fclose(file);
            return 1;
        }
        pos += (long)sizeof(GameState);
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    if (fwrite(game, sizeof(GameState), 1, file) != 1) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

/* Lists only saves that belong to the logged-in user
   Sadece giriş yapan kullanıcıya ait kayıtları listeler

   Çalışma mantığı:
   - Dosyadaki tüm GameState kayıtlarını okur
   - Username eşleşen kayıtları ekrana yazar

   Ekrana yazdığı bilgiler:
   - GameId
   - Turn

   Böylece kullanıcının sadece kendi kayıtlarını görmesi sağlanır. */
void list_user_saves(const char *username) {
    FILE *file = fopen(SAVES_FILE, "rb");
    GameState game;
    int found = 0;

    if (file == NULL) {
        printf("No save file found yet.\n");
        return;
    }

    while (fread(&game, sizeof(GameState), 1, file) == 1) {
        if (strcmp(game.username, username) == 0) {
            printf("GameId: %d, Turn: %s\n", game.game_id, game.current_turn == BLACK ? "Black" : "White");
            found = 1;
        }
    }

    if (!found) {
        printf("You do not have any saves.\n");
    }

    fclose(file);
}

/* Loads one selected save and verifies checksum before allowing play
   Seçilen kaydı yükler ve oyuna izin vermeden önce checksum doğrular

   Adımlar:
   1. Dosyayı açar
   2. Kullanıcıya ait kayıtları listeler
   3. Kullanıcıdan GameId ister
   4. Dosyada o game_id ve username ile eşleşen kaydı arar
   5. Bulduğunda checksum doğrular
   6. Doğruysa oyunu yükler
   7. Yanlışsa "tampered/corrupted" hatası verir

   Bu fonksiyon ödevin güvenlik kısmını karşılar. */
int load_game_record(const char *username, GameState *game) {
    FILE *file = fopen(SAVES_FILE, "rb");
    GameState stored;
    char input[32];
    int selected_id;

    if (file == NULL) {
        printf("No saved games found.\n");
        return 0;
    }

    list_user_saves(username);
    if (!read_line("Enter the GameId to load: ", input, sizeof(input))) {
        fclose(file);
        return 0;
    }

    selected_id = atoi(input);

    rewind(file);
    while (fread(&stored, sizeof(GameState), 1, file) == 1) {
        if (stored.game_id == selected_id && strcmp(stored.username, username) == 0) {
            if (compute_game_checksum(&stored) != stored.checksum) {
                printf("Error: Save file corrupted or tampered with.\n");
                fclose(file);
                return 0;
            }
            *game = stored;
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    printf("Matching save not found for this user.\n");
    return 0;
}

/* Converts input like D3 into array indexes / D3 gibi girişi dizi indislerine çevirir

   Örnek:
   "D3" -> row = 3, col = 2

   Çalışma mantığı:
   - İlk karakter harf mi kontrol eder
   - İkinci karakter rakam mı kontrol eder
   - Harfi satıra çevirir
   - Rakamı sütuna çevirir */
int parse_move_input(const char *input, int *row, int *col) {
    if (strlen(input) < 2) {
        return 0;
    }

    if (isalpha((unsigned char)input[0]) && isdigit((unsigned char)input[1])) {
        *row = toupper((unsigned char)input[0]) - 'A';
        *col = input[1] - '1';
        return is_on_board(*row, *col);
    }

    return 0;
}

/* Updates win/loss counters after game ends / Oyun bitince kazanma-kaybetme sayaçlarını günceller

   Çalışma mantığı:
   - result > 0 ise games_won artar
   - result < 0 ise games_lost artar
   - beraberlikte değişiklik olmaz

   Sonrasında update_user_record ile dosyada güncellenir. */
void update_score_after_game(UserRecord *user, long user_pos, int result) {
    if (result > 0) {
        user->games_won++;
    } else if (result < 0) {
        user->games_lost++;
    }

    if (!update_user_record(user, user_pos)) {
        printf("Warning: Could not update account statistics.\n");
    }
}

/* Greedy AI: choose the move that flips the most discs
   Açgözlü yapay zeka: en fazla taşı çeviren hamleyi seçer

   Bu, bilgisayarın hamlesini yapan fonksiyondur.

   Mantık:
   - Tahtadaki tüm kareleri gezer
   - Her hücre için collect_flips çağırır
   - En fazla taş çevirecek hamleyi bulur
   - O hamleyi uygular

   Bu tam olarak greedy AI mantığıdır.
   Hocanın istediği "en çok taş çeviren hamleyi seç" şartını karşılar. */
int computer_play(GameState *game) {
    int row;
    int col;
    int best_row = -1;
    int best_col = -1;
    int best_flips = -1;
    Position flips[BOARD_SIZE * BOARD_SIZE];

    for (row = 0; row < BOARD_SIZE; row++) {
        for (col = 0; col < BOARD_SIZE; col++) {
            int flip_count = collect_flips(game->board, row, col, WHITE, flips, BOARD_SIZE * BOARD_SIZE);
            if (flip_count > best_flips) {
                best_flips = flip_count;
                best_row = row;
                best_col = col;
            }
        }
    }

    if (best_flips <= 0) {
        return 0;
    }

    apply_move(game->board, best_row, best_col, WHITE);
    printf("Computer played %c%d and flipped %d piece(s).\n", 'A' + best_row, best_col + 1, best_flips);
    return 1;
}

/* Prints final result and updates account stats / Sonucu yazdırır ve hesap istatistiklerini günceller

   Oyun sonunda:
   - taşları sayar
   - tahtayı yazdırır
   - skoru gösterir
   - kazananı belirler
   - kullanıcı istatistiklerini günceller

   Siyah fazla ise oyuncu kazanır.
   Beyaz fazla ise bilgisayar kazanır. */
void show_game_result(GameState *game, UserRecord *user, long user_pos) {
    int black_count;
    int white_count;
    int empty_count;

    count_discs(game->board, &black_count, &white_count, &empty_count);
    print_board(game->board);
    printf("Final score -> Black: %d, White: %d\n", black_count, white_count);

    if (black_count > white_count) {
        printf("You win.\n");
        update_score_after_game(user, user_pos, 1);
    } else if (white_count > black_count) {
        printf("Computer wins.\n");
        update_score_after_game(user, user_pos, -1);
    } else {
        printf("The game is a draw.\n");
    }
}

/* Main gameplay loop for human vs computer / Oyuncu ve bilgisayar arasındaki ana oyun döngüsü

   Bu, oyunun ana çalışan kısmıdır.

   Her turda yapılanlar:
   1. İnsan oyuncunun geçerli hamlesi var mı kontrol edilir
   2. Bilgisayarın geçerli hamlesi var mı kontrol edilir
   3. Taşlar sayılır
   4. Tahta doldu mu veya iki tarafın da hamlesi kalmadı mı kontrol edilir
      - evet ise oyun biter
   5. Tahta ve skor gösterilir

   İnsan turuysa:
   - hamlesi yoksa sıra bilgisayara geçer
   - kullanıcıdan şu girişler alınabilir:
     * save
     * saveas
     * quit
     * D3 gibi normal hamle
   - hamle geçerliyse uygulanır
   - sıra bilgisayara geçer

   Bilgisayar turuysa:
   - hamlesi yoksa sıra insana geçer
   - hamlesi varsa computer_play() çağrılır
   - sonra sıra insana geçer */
void play_game(GameState *game, UserRecord *user, long user_pos) {
    char input[32];
    int black_count;
    int white_count;
    int empty_count;

    while (1) {
        int human_has_move = has_valid_move(game->board, BLACK);
        int computer_has_move = has_valid_move(game->board, WHITE);

        count_discs(game->board, &black_count, &white_count, &empty_count);
        if (empty_count == 0 || (!human_has_move && !computer_has_move)) {
            show_game_result(game, user, user_pos);
            return;
        }

        print_board(game->board);
        printf("Score -> Black: %d, White: %d\n", black_count, white_count);

        /* Black is the human player / Siyah taşları insan oyuncu kullanır */
        if (game->current_turn == BLACK) {
            int row;
            int col;

            if (!human_has_move) {
                printf("You have no valid move. Turn passes to computer.\n");
                game->current_turn = WHITE;
                continue;
            }

            printf("Your turn. Type a move like D3, or 'save', 'saveas', 'quit'.\n");
            if (!read_line("> ", input, sizeof(input))) {
                return;
            }

            if (strings_equal_ignore_case(input, "save")) {
                if (save_game_record(game, 0)) {
                    printf("Game saved with GameId %d.\n", game->game_id);
                } else {
                    printf("Save failed.\n");
                }
                continue;
            }

            if (strings_equal_ignore_case(input, "saveas")) {
                if (save_game_record(game, 1)) {
                    printf("Game saved as a new record with GameId %d.\n", game->game_id);
                } else {
                    printf("Save As failed.\n");
                }
                continue;
            }

            if (strings_equal_ignore_case(input, "quit")) {
                printf("Returning to dashboard without saving.\n");
                return;
            }

            if (!parse_move_input(input, &row, &col)) {
                printf("Invalid input format.\n");
                continue;
            }

            if (!apply_move(game->board, row, col, BLACK)) {
                printf("Illegal move.\n");
                continue;
            }

            game->current_turn = WHITE;
        } else {
            /* White is the computer / Beyaz taşları bilgisayar oynar */
            if (!computer_has_move) {
                printf("Computer has no valid move. Turn passes to you.\n");
                game->current_turn = BLACK;
                continue;
            }

            printf("Computer is thinking...\n");
            computer_play(game);
            game->current_turn = BLACK;
        }
    }
}

/* Menu shown after successful login / Başarılı girişten sonra gösterilen menü

   Menü seçenekleri:
   - New Game
   - Load Game
   - List My Saves
   - Logout
   - Quit Program

   Giriş sonrası kullanıcıyı yöneten ana menüdür. */
void show_dashboard(UserRecord *user, long user_pos) {
    char choice[16];

    while (1) {
        GameState game;

        printf("\n=== User Dashboard ===\n");
        printf("Player: %s | Wins: %d | Losses: %d\n", user->username, user->games_won, user->games_lost);
        printf("1. New Game\n");
        printf("2. Load Game\n");
        printf("3. List My Saves\n");
        printf("4. Logout\n");
        printf("5. Quit Program\n");

        if (!read_line("Choice: ", choice, sizeof(choice))) {
            return;
        }

        switch (atoi(choice)) {
            case 1:
                initialize_board(&game, user->username);
                play_game(&game, user, user_pos);
                break;
            case 2:
                if (load_game_record(user->username, &game)) {
                    printf("Game loaded successfully.\n");
                    play_game(&game, user, user_pos);
                }
                break;
            case 3:
                list_user_saves(user->username);
                break;
            case 4:
                return;
            case 5:
                exit(0);
            default:
                printf("Invalid menu choice.\n");
        }
    }
}

/* Program entry point / Programın başlangıç noktası */
int main(void) {
    char choice[16];

    while (1) {
        UserRecord current_user;
        long user_pos = -1;

        printf("\n=== Reversi Welcome Screen ===\n");
        printf("1. Create Account\n");
        printf("2. Login\n");
        printf("3. Quit\n");

        if (!read_line("Choice: ", choice, sizeof(choice))) {
            break;
        }

        switch (atoi(choice)) {
            case 1:
                create_account();
                break;
            case 2:
                if (login_user(&current_user, &user_pos)) {
                    show_dashboard(&current_user, user_pos);
                    if (find_user_record(current_user.username, &current_user, &user_pos)) {
                        continue;
                    }
                }
                break;
            case 3:
                printf("Goodbye.\n");
                return 0;
            default:
                printf("Invalid menu choice.\n");
        }
    }

    return 0;
}

/*
KISA KAVRAM ÖZETLERİ

Hashlemek:
Bir bilgiyi alıp onu başka bir sayıya ya da değere dönüştürmektir.
Örnek:
PIN = 1234
Dosyada 1234 olarak değil, hashlenmiş hali saklanır.

djb2:
Bu dönüşümü yapan basit hash algoritmasıdır.

rewind(file):
Dosya işaretçisini tekrar dosyanın başına götürür.

fseek(file, ..., ...):
Dosyada istenilen konuma gitmeyi sağlar.
Yani:
- rewind = sadece başa dön
- fseek = istediğin yere git

checksum:
Save dosyasıyla oynanmış mı anlamak için kullanılan kontrol değeridir.

save:
Mevcut kaydı günceller.

save as:
Yeni kayıt oluşturur.

Greedy AI:
Bilgisayarın o anda en fazla taşı çevirecek hamleyi seçmesidir.
*/