// Contains logic to handle files in Spanish.
//
// For now, the only entry point is `CountSyllables`.
namespace es {
bool IsConsonant(string c) { return "bcdfghjklmnñpqrstvwxyz".find(c, 0) >= 0; }
bool IsVowel(string c) { return "aeiouáéíóú".find(c, 0) >= 0; }

number SkipVowels(string word, number position) {
  while (position < word.size() && !IsConsonant(word.substr(position, 1)))
    position++;
  return position;
}

number SkipConsonants(string word, number position) {
  while (position < word.size() && IsConsonant(word.substr(position, 1)))
    position++;
  return position;
}

bool IsStrong(string c) { return "aeoáéó".find(c, 0) >= 0; }

bool IsHiatus(string word) {
  string first = word.substr(0, 1);
  string second = word.substr(1, 1);
  if (IsStrong(first) && IsStrong(second)) return true;  // Hiato simple.
  if (("íú".find(first, 0) >= 0 && IsStrong(second)) ||
      (IsStrong(first) && "íú".find(second, 0) >= 0))
    return true;  // Hiato acentual.
  return false;
}

VectorString Syllables(string word) {
  VectorString output = VectorString();
  number position = 0;
  while (position < word.size()) {
    number next_vowel = SkipConsonants(word, position);
    number end = SkipVowels(word, next_vowel);
    if (end >= next_vowel + 2 && IsHiatus(word.substr(next_vowel, 2))) {
      // In the hiatus, we always separate the vowels.
      end = next_vowel + 1;
    } else if (end >= next_vowel + 3 &&
               IsHiatus(word.substr(next_vowel + 1, 2))) {
      end = next_vowel + 2;
    } else if (end < word.size()) {
      number len_following_consonants = SkipConsonants(word, end) - end;
      if (end + len_following_consonants == word.size()) {
        // If there are no vowels left in the word, this is the last syllable.
        end = word.size();
      } else if (len_following_consonants == 2) {
        string consonants = word.substr(end, 2);
        if (consonants != "pr" && consonants != "br" && consonants != "dr" &&
            consonants != "cr" && consonants != "fr" && consonants != "gr" &&
            consonants != "kr" && consonants != "tr" && consonants != "fl" &&
            consonants != "pl" && consonants != "gl" && consonants != "kl" &&
            consonants != "cl" && consonants != "bl" && consonants != "tl")
          // Break the two consonants; one goes to each surrounding vowel.
          // Otherwise (the cases listed above), they both go to the next vowel.
          //
          // Interestingly, "tl" is non-standard. In Spain, it's more common to
          // split it. Not so in Colombia.
          end = end + 1;
      } else if (len_following_consonants == 3) {
        string tail = word.substr(end + 1, 2);
        if ("pbcgtd".find(word.substr(end + 1, 1), 0) >= 0 &&
            "lr".find(word.substr(end + 2, 1), 0) >= 0)
          end = end + 1;
        else
          end = end + 2;
      } else if (len_following_consonants == 4) {
        end = end + 2;
      }
    }
    if (end < word.size()) {
      string candidate = word.substr(end - 1, 2);
      if (candidate == "ch" || candidate == "ll" || candidate == "gu" ||
          candidate == "qu" || candidate == "rr")
        end--;  // Handle a digraph (ch, ll).
    }
    output.push_back(word.substr(position, end - position));
    position = end;
  }
  return output;
}

string ShowSyllables(VectorString input) {
  string output = "";
  string separator = "";
  for (number i = 0; i < input.size(); i++) {
    output += separator + input.get(i);
    separator = "-";
  }
  return output;
}

string Validate(string input, string expectation) {
  string result = ShowSyllables(Syllables(input));
  return result == expectation ? "" : " " + result;
}

string Validate() {
  return Validate("florentino", "flo-ren-ti-no") + Validate("mafia", "ma-fia") +
         Validate("campeonato", "cam-pe-o-na-to") +
         Validate("barcelona", "bar-ce-lo-na") +
         Validate("historia", "his-to-ria") +
         Validate("constipación", "cons-ti-pa-ción") +
         Validate("príncipes", "prín-ci-pes") +
         Validate("español", "es-pa-ñol") + Validate("fútbol", "fút-bol") +
         Validate("herramientas", "he-rra-mien-tas") +
         Validate("cooperación", "co-o-pe-ra-ción") +
         Validate("conquistas", "con-quis-tas") +
         Validate("complacer", "com-pla-cer") +
         Validate("planteamiento", "plan-te-a-mien-to") +
         Validate("independencia", "in-de-pen-den-cia") +
         Validate("averiguáis", "a-ve-ri-guáis") +
         Validate("productividad", "pro-duc-ti-vi-dad") +
         Validate("regimiento", "re-gi-mien-to") +
         Validate("tecnología", "tec-no-lo-gí-a") +
         Validate("diario", "dia-rio") + Validate("madrid", "ma-drid") +
         Validate("pasado", "pa-sa-do") + Validate("cenit", "ce-nit") +
         Validate("población", "po-bla-ción") +
         Validate("bonanza", "bo-nan-za") +
         Validate("imágenes", "i-má-ge-nes") + Validate("regla", "re-gla") +
         Validate("constelación", "cons-te-la-ción") +
         Validate("títulos", "tí-tu-los") + Validate("paella", "pa-e-lla") +
         Validate("selector", "se-lec-tor") +
         Validate("cuarenta", "cua-ren-ta") +
         Validate("cosmología", "cos-mo-lo-gí-a") +
         Validate("referencia", "re-fe-ren-cia") +
         Validate("vigía", "vi-gí-a") + Validate("francia", "fran-cia") +
         Validate("corresponsal", "co-rres-pon-sal") +
         Validate("juventud", "ju-ven-tud") + Validate("opinión", "o-pi-nión") +
         Validate("bloqueo", "blo-que-o") +
         Validate("avalancha", "a-va-lan-cha") +
         Validate("ventilador", "ven-ti-la-dor") +
         Validate("desplazamiento", "des-pla-za-mien-to") +
         Validate("hallar", "ha-llar") + Validate("cosmos", "cos-mos") +
         Validate("periódico", "pe-rió-di-co") +
         Validate("igualdad", "i-gual-dad") +
         Validate("plantación", "plan-ta-ción") +
         Validate("obstruyendo", "obs-tru-yen-do") +
         Validate("chile", "chi-le") + Validate("chantajear", "chan-ta-je-ar") +
         Validate("hallemos", "ha-lle-mos") +
         Validate("seguidor", "se-gui-dor") + Validate("queso", "que-so") +
         Validate("corromper", "co-rrom-per") + Validate("aéreo", "a-é-re-o") +
         Validate("peleé", "pe-le-é") + Validate("tranvía", "tran-ví-a") +
         Validate("opioide", "o-pioi-de") + Validate("actuáis", "ac-tuáis") +
         Validate("aurora", "au-ro-ra") + Validate("cuando", "cuan-do") +
         Validate("cuidado", "cui-da-do") + Validate("día", "dí-a") +
         Validate("gavilán", "ga-vi-lán") + Validate("bíceps", "bí-ceps") +
         Validate("elegir", "e-le-gir") + Validate("colina", "co-li-na") +
         Validate("zamuro", "za-mu-ro") + Validate("alegría", "a-le-grí-a") +
         Validate("ladrido", "la-dri-do") +
         Validate("cofradía", "co-fra-dí-a") +
         Validate("reactor", "re-ac-tor") +
         Validate("hipnotizado", "hip-no-ti-za-do") +
         Validate("atletismo", "a-tle-tis-mo") +
         Validate("constancia", "cons-tan-cia") +
         Validate("compraré", "com-pra-ré") + Validate("enclave", "en-cla-ve") +
         Validate("obstrucción", "obs-truc-ción") +
         Validate("construcción", "cons-truc-ción");
}

VectorString BreakWords(string line) {
  VectorString output = VectorString();
  number position = 0;
  while (position < line.size()) {
    if (!IsVowel(line.substr(position, 1)) &&
        !IsConsonant(line.substr(position, 1))) {
      position++;
    } else {
      number len = 1;
      while (position + len < line.size() &&
             (IsVowel(line.substr(position + len, 1)) ||
              IsConsonant(line.substr(position + len, 1))))
        len++;
      output.push_back(line.substr(position, len));
      position += len;
    }
  }
  return output;
}

bool CanJoinNextWordSynalepha(string word) {
  return IsVowel(word.substr(word.size() - 1, 1)) || word == "y";
}

bool CanJoinPreviousWordSynalepha(string word) {
  return IsVowel(word.substr(0, 1)) || word == "y" ||
         (word.substr(0, 1) == "h" && word.size() >= 2 &&
          IsVowel(word.substr(1, 1)));
}

// Adds to each line metadata with the count of the number of syllables it
// contains (after applying synalepha to join syllables of different words).
void CountSyllables(Buffer buffer) {
  buffer.AddLineProcessor("s", [](string line) -> string {
    number count = 0;
    VectorString words = BreakWords(line.tolower());
    for (number i = 0; i < words.size(); i++) {
      string word = words.get(i);
      count += Syllables(word).size();
      if (i > 0 && CanJoinNextWordSynalepha(words.get(i - 1)) &&
          CanJoinPreviousWordSynalepha(word) &&
          (word != "y" || i + 1 == words.size() ||
           !CanJoinPreviousWordSynalepha(words.get(i + 1))))
        count--;
    }
    return count.tostring();
  });
}
}  // namespace es
