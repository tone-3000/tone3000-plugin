#include "OAuth.h"

#include <juce_cryptography/juce_cryptography.h>

namespace t3k::ui::oauth {

juce::String base64url(const void* data, size_t size) {
  return juce::Base64::toBase64(data, size).replaceCharacter('+', '-').replaceCharacter('/', '_').removeCharacters("=");
}

namespace {
juce::String randomBase64url(int bytes) {
  juce::MemoryBlock block(static_cast<size_t>(bytes));
  auto& rng = juce::Random::getSystemRandom();
  for (size_t i = 0; i < block.getSize(); ++i) block[i] = static_cast<char>(rng.nextInt(256));
  return base64url(block.getData(), block.getSize());
}

juce::StringPairArray parseQuery(const juce::String& query) {
  juce::StringPairArray params;
  const auto q = query.startsWithChar('?') ? query.substring(1) : query;
  for (const auto& pair : juce::StringArray::fromTokens(q, "&", {})) {
    if (pair.isEmpty()) continue;
    const auto key = juce::URL::removeEscapeChars(pair.upToFirstOccurrenceOf("=", false, false).replaceCharacter('+', ' '));
    const auto value = pair.containsChar('=')
                           ? juce::URL::removeEscapeChars(pair.fromFirstOccurrenceOf("=", false, false).replaceCharacter('+', ' '))
                           : juce::String();
    params.set(key, value);
  }
  return params;
}
}  // namespace

juce::String Pkce::challengeFor(const juce::String& verifier) {
  const juce::SHA256 hash(verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
  const auto block = hash.getRawData();
  return base64url(block.getData(), block.getSize());
}

Pkce Pkce::generate() {
  Pkce p;
  p.verifier = randomBase64url(32);
  p.challenge = challengeFor(p.verifier);
  p.state = randomBase64url(16);
  return p;
}

juce::String authorizeUrl(const juce::String& origin, const juce::String& clientId, const juce::String& redirectUri,
                          const Pkce& pkce, const juce::StringPairArray& extra) {
  auto url = juce::URL(origin + "/api/v1/oauth/authorize")
                 .withParameter("client_id", clientId)
                 .withParameter("redirect_uri", redirectUri)
                 .withParameter("response_type", "code")
                 .withParameter("code_challenge", pkce.challenge)
                 .withParameter("code_challenge_method", "S256")
                 .withParameter("state", pkce.state);
  for (const auto& key : extra.getAllKeys()) url = url.withParameter(key, extra[key]);
  return url.toString(true);
}

Callback Callback::parse(const juce::String& query, const juce::String& expectedState) {
  const auto params = parseQuery(query);
  Callback cb;
  cb.code = params["code"];
  const bool canceled = params["canceled"] == "true";
  auto fail = [&](juce::String why) {
    cb.kind = Kind::error;
    cb.error = std::move(why);
    return cb;
  };
  if (params["state"] != expectedState) return fail("state_mismatch");
  if (canceled && cb.code.isEmpty()) {
    cb.kind = Kind::canceled;
    return cb;
  }
  if (params["error"].isNotEmpty()) return fail(params["error"]);
  if (cb.code.isEmpty()) return fail("missing_code");
  cb.kind = Kind::code;
  return cb;
}

}  // namespace t3k::ui::oauth
