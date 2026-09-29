# MobileLab developer entry points. `make help` lists them.

.DEFAULT_GOAL := help
NODE_DIRS := backend cli

.PHONY: help setup dev demo build check test backend-test cli-test clean

help: ## Show this help
	@awk 'BEGIN {FS = ":.*##"} /^[a-zA-Z_-]+:.*##/ {printf "  \033[1m%-14s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)

setup: ## Install backend and CLI dependencies
	cd backend && npm ci
	cd cli && npm ci

dev: ## Run the backend with auto-reload (dashboard at http://127.0.0.1:4000)
	cd backend && npm run dev

demo: ## Run the backend in demo mode: simulator commands are simulated, works on any OS
	cd backend && IOSLAB_SIMULATOR_MOCK=true npm run dev

build: ## Compile backend and CLI
	cd backend && npm run build
	cd cli && npm run build

check: ## Type-check backend and CLI (including tests)
	cd backend && npm run check
	cd cli && npm run check

test: backend-test cli-test ## Run all Node tests

backend-test:
	cd backend && npm test

cli-test:
	cd cli && npm test

clean: ## Remove build output
	rm -rf backend/dist cli/dist
