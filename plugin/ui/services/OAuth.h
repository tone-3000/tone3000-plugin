// The pure parts of the TONE3000 OAuth flows (port of tone3000-client.ts
// buildPkceParams / buildAuthorizeUrl / handleOAuthCallback's parsing): PKCE
// material, the authorize URL, and the redirect's query string decoded into
// an outcome. No I/O here, so SelfTests cover it directly.
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui::oauth {

struct Pkce {
  juce::String verifier;   // 32 random bytes, base64url
  juce::String challenge;  // base64url(SHA-256(verifier))
  juce::String state;      // 16 random bytes, base64url

  static Pkce generate();
  // Exposed for the RFC 7636 test vector.
  static juce::String challengeFor(const juce::String& verifier);
};

juce::String base64url(const void* data, size_t size);

// {origin}/api/v1/oauth/authorize?client_id&redirect_uri&response_type=code
// &code_challenge&code_challenge_method=S256&state[&extra…]
juce::String authorizeUrl(const juce::String& origin, const juce::String& clientId, const juce::String& redirectUri,
                          const Pkce& pkce, const juce::StringPairArray& extra);

// The redirect back from tone3000.com, parsed and checked against the
// stored state (the web's handleOAuthCallback up to the token exchange).
struct Callback {
  enum class Kind {
    code,      // exchange `code`
    canceled,  // the user backed out without signing in: back to idle
    error      // `error` names it: state_mismatch, missing_code, or the server's
  };
  Kind kind = Kind::error;
  juce::String code;
  juce::String error;

  // `query` is the redirect URL's query string (with or without the '?').
  static Callback parse(const juce::String& query, const juce::String& expectedState);
};

}  // namespace t3k::ui::oauth
