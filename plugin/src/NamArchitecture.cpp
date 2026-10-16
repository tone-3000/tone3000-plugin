#include "NamArchitecture.h"

namespace nam_arch {

bool namedNotA2(const juce::String& name) {
  for (auto word : juce::StringArray::fromTokens(name, " \t", "")) {
    // At most in brackets: "(A1)", "[xSTD]".
    while (word.isNotEmpty() && (word[0] == '(' || word[0] == '['))
      word = word.substring(1);
    while (word.isNotEmpty() && (word.getLastCharacter() == ')' || word.getLastCharacter() == ']'))
      word = word.dropLastCharacters(1);
    if (word.equalsIgnoreCase("A1") || word.equalsIgnoreCase("REVyHI") || word.equalsIgnoreCase("xSTD"))
      return true;
  }
  return false;
}

}  // namespace nam_arch
