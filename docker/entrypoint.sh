#!/bin/sh
# No arguments (or "mcp"): the MCP server on stdio. "render", "search" or
# "part": the CLI. Anything else runs as a command.
set -e
case "$1" in
  "" | mcp) exec node /opt/fritzing-render/mcp/src/server.ts ;;
  render | search | part) exec fritzing-render "$@" ;;
  *) exec "$@" ;;
esac
