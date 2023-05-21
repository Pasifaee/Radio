// Polecenie do wysłania muzyki: // TODO poprawne? - skopiuj z treści
// sox -S "just_boring.mp3" -r 44100 -b 16 -e signed-integer -c 2 -t raw - | pv -q -L 176400 | ./sikradio-sender -a 127.0.0.1 -n "Radio Muzyczka"

// TODO:
//   testy:
//   - najpierw po prostu w jednym senderze zrób read i write i zobacz czy ładnie słychać muzykę
//   - potem zrób przetwarzanie na i z datagramów ale nie przez sieć ?
//   generalnie testy które pozwolą wykryć na którym etapie coś się spierdala

#include "sikradio-sender.h"
#include "sikradio-receiver.h"

int main() {}