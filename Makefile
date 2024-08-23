BUILD_DIR = o

# build: all

# build:
all:
	cmake -B o/ -S .  -G Ninja
	cd $(BUILD_DIR) && ninja

setup:
	cp sanitas/GitHooks/pre-commit .git/hooks/pre-commit
	chmod +x .git/hooks/pre-commit
	
