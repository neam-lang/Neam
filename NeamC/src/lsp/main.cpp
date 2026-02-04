// SPDX-License-Identifier: Apache-2.0
//
// Neam LSP - entrypoint
//

#include "neamc/lsp/server.hpp"

int main()
{
  neamc::lsp::Server server;
  server.run();
  return 0;
}
