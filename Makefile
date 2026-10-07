CC ?= cc
CFLAGS ?= -O2 -Wall

TARGET = mqtt_pub
SERVICE = mqtt_pub.service
CONF_FILE = mqtt_pub.ini
ENV_FILE = /etc/default/mqtt_pub
BIN_DEST = /usr/local/bin

# Default parameters (overridable via command line)
HOST ?= 127.0.0.1
PORT ?= 1883
MQTT_USER ?=
MQTT_PASS ?=
CONF ?= /etc

.PHONY: all install uninstall clean distclean

all: $(TARGET)

$(TARGET): mqtt_pub.c
	$(CC) $(CFLAGS) -o $(TARGET) mqtt_pub.c -lmosquitto -lpthread

install: $(TARGET)
	@set -e; \
	ACTUAL_CONF="$(CONF)"; \
	LOGGED_CONF=""; \
	if [ -f install.log ]; then \
		LOGGED_CONF=$$(grep -oP 'CONF=\K.*' install.log 2>/dev/null || echo ""); \
		if [ "$(origin CONF)" = "file" ]; then \
			if [ -n "$$LOGGED_CONF" ]; then \
				ACTUAL_CONF="$$LOGGED_CONF"; \
				echo "[install] CONF from install.log: $$ACTUAL_CONF"; \
			else \
				ACTUAL_CONF="/etc"; \
				echo "[install] install.log has empty CONF, using default: /etc"; \
			fi; \
		else \
			echo "[install] CONF overridden via command line: $$ACTUAL_CONF"; \
		fi; \
	else \
		echo "[install] no install.log, using default: $$ACTUAL_CONF"; \
	fi; \
	ACTUAL_CONF_FILE="$$ACTUAL_CONF/$(CONF_FILE)"; \
	echo "[install] config path: $$ACTUAL_CONF_FILE"; \
	\
	echo "[install] stopping service if running..."; \
	systemctl stop mqtt_pub 2>/dev/null || true; \
	\
	echo "[install] installing binary..."; \
	install -d $(BIN_DEST); \
	install -m 755 $(TARGET) $(BIN_DEST)/$(TARGET); \
	\
	echo "[install] installing systemd unit..."; \
	install -d /etc/systemd/system; \
	install -m 644 $(SERVICE) /etc/systemd/system/$(SERVICE); \
	\
	if [ -f "$(CONF_FILE)" ]; then \
		echo "[install] found local $(CONF_FILE) in project dir, copying to $$ACTUAL_CONF_FILE"; \
		install -d "$$ACTUAL_CONF"; \
		install -m 640 "$(CONF_FILE)" "$$ACTUAL_CONF_FILE"; \
		UPDATED=0; \
		if [ "$(origin HOST)" = "command line" ]; then \
			sed -i "s/^host=.*/host=$(HOST)/" "$$ACTUAL_CONF_FILE"; \
			echo "[install] updated host=$(HOST)"; \
			UPDATED=1; \
		fi; \
		if [ "$(origin PORT)" = "command line" ]; then \
			sed -i "s/^port=.*/port=$(PORT)/" "$$ACTUAL_CONF_FILE"; \
			echo "[install] updated port=$(PORT)"; \
			UPDATED=1; \
		fi; \
		if [ "$(origin MQTT_USER)" = "command line" ]; then \
			if [ -n "$(MQTT_USER)" ]; then \
				if grep -q "^user=" "$$ACTUAL_CONF_FILE"; then \
					sed -i "s/^user=.*/user=$(MQTT_USER)/" "$$ACTUAL_CONF_FILE"; \
				else \
					sed -i "/^\[MQTT\]/a user=$(MQTT_USER)" "$$ACTUAL_CONF_FILE"; \
				fi; \
				echo "[install] updated user=$(MQTT_USER)"; \
			else \
				sed -i "/^user=/d" "$$ACTUAL_CONF_FILE"; \
				echo "[install] removed user"; \
			fi; \
			UPDATED=1; \
		fi; \
		if [ "$(origin MQTT_PASS)" = "command line" ]; then \
			if [ -n "$(MQTT_PASS)" ]; then \
				if grep -q "^password=" "$$ACTUAL_CONF_FILE"; then \
					sed -i "s/^password=.*/password=$(MQTT_PASS)/" "$$ACTUAL_CONF_FILE"; \
				else \
					sed -i "/^\[MQTT\]/a password=$(MQTT_PASS)" "$$ACTUAL_CONF_FILE"; \
				fi; \
				echo "[install] updated password"; \
			else \
				sed -i "/^password=/d" "$$ACTUAL_CONF_FILE"; \
				echo "[install] removed password"; \
			fi; \
			UPDATED=1; \
		fi; \
		if [ $$UPDATED -eq 0 ]; then \
			echo "[install] no config overrides applied"; \
		fi; \
	elif [ ! -f "$$ACTUAL_CONF_FILE" ]; then \
		echo "[install] creating config: $$ACTUAL_CONF_FILE"; \
		install -d "$$ACTUAL_CONF"; \
		{ \
			echo "# MQTT Publisher configuration"; \
			echo ""; \
			echo "[MQTT]"; \
			echo "host=$(HOST)"; \
			echo "port=$(PORT)"; \
			if [ -n "$(MQTT_USER)" ]; then echo "user=$(MQTT_USER)"; fi; \
			if [ -n "$(MQTT_PASS)" ]; then echo "password=$(MQTT_PASS)"; fi; \
			echo "debug=0"; \
		} > "$$ACTUAL_CONF_FILE"; \
		chmod 640 "$$ACTUAL_CONF_FILE"; \
		echo "[install] config created"; \
	else \
		echo "[install] config exists: $$ACTUAL_CONF_FILE"; \
		UPDATED=0; \
		if [ "$(origin HOST)" = "command line" ]; then \
			sed -i "s/^host=.*/host=$(HOST)/" "$$ACTUAL_CONF_FILE"; \
			echo "[install] updated host=$(HOST)"; \
			UPDATED=1; \
		fi; \
		if [ "$(origin PORT)" = "command line" ]; then \
			sed -i "s/^port=.*/port=$(PORT)/" "$$ACTUAL_CONF_FILE"; \
			echo "[install] updated port=$(PORT)"; \
			UPDATED=1; \
		fi; \
		if [ "$(origin MQTT_USER)" = "command line" ]; then \
			if [ -n "$(MQTT_USER)" ]; then \
				if grep -q "^user=" "$$ACTUAL_CONF_FILE"; then \
					sed -i "s/^user=.*/user=$(MQTT_USER)/" "$$ACTUAL_CONF_FILE"; \
				else \
					sed -i "/^\[MQTT\]/a user=$(MQTT_USER)" "$$ACTUAL_CONF_FILE"; \
				fi; \
				echo "[install] updated user=$(MQTT_USER)"; \
			else \
				sed -i "/^user=/d" "$$ACTUAL_CONF_FILE"; \
				echo "[install] removed user"; \
			fi; \
			UPDATED=1; \
		fi; \
		if [ "$(origin MQTT_PASS)" = "command line" ]; then \
			if [ -n "$(MQTT_PASS)" ]; then \
				if grep -q "^password=" "$$ACTUAL_CONF_FILE"; then \
					sed -i "s/^password=.*/password=$(MQTT_PASS)/" "$$ACTUAL_CONF_FILE"; \
				else \
					sed -i "/^\[MQTT\]/a password=$(MQTT_PASS)" "$$ACTUAL_CONF_FILE"; \
				fi; \
				echo "[install] updated password"; \
			else \
				sed -i "/^password=/d" "$$ACTUAL_CONF_FILE"; \
				echo "[install] removed password"; \
			fi; \
			UPDATED=1; \
		fi; \
		if [ $$UPDATED -eq 0 ]; then \
			echo "[install] no config changes"; \
		fi; \
	fi; \
	\
	echo "[install] updating env file..."; \
	install -d /etc/default; \
	if [ ! -f "$(ENV_FILE)" ]; then \
		echo "PARAMS=-f $$ACTUAL_CONF_FILE" > $(ENV_FILE); \
		echo "[install] created env file: $(ENV_FILE)"; \
	else \
		sed -i "s|^PARAMS=.*|PARAMS=-f $$ACTUAL_CONF_FILE|" $(ENV_FILE) 2>/dev/null || true; \
		if ! grep -q "^PARAMS=" $(ENV_FILE); then \
			echo "PARAMS=-f $$ACTUAL_CONF_FILE" >> $(ENV_FILE); \
		fi; \
		echo "[install] updated env file: $(ENV_FILE)"; \
	fi; \
	\
	echo "CONF=$$ACTUAL_CONF" > install.log; \
	echo "[install] saved install.log"; \
	\
	echo "[install] reloading systemd..."; \
	systemctl daemon-reload; \
	if systemctl is-active --quiet mqtt_pub; then \
		systemctl restart mqtt_pub; \
		echo "[install] service restarted"; \
	else \
		systemctl enable mqtt_pub; \
		systemctl start mqtt_pub; \
		echo "[install] service enabled and started"; \
	fi; \
	echo "[install] done"

uninstall:
	@set -e; \
	ACTUAL_CONF="/etc"; \
	if [ -f install.log ]; then \
		LOGGED_CONF=$$(grep -oP 'CONF=\K.*' install.log 2>/dev/null || echo ""); \
		if [ -n "$$LOGGED_CONF" ]; then \
			ACTUAL_CONF="$$LOGGED_CONF"; \
		fi; \
	fi; \
	ACTUAL_CONF_FILE="$$ACTUAL_CONF/$(CONF_FILE)"; \
	echo "[uninstall] stopping service..."; \
	systemctl stop mqtt_pub 2>/dev/null || true; \
	systemctl disable mqtt_pub 2>/dev/null || true; \
	echo "[uninstall] removing files..."; \
	rm -f $(BIN_DEST)/$(TARGET); \
	rm -f /etc/systemd/system/$(SERVICE); \
	rm -f $(ENV_FILE); \
	rm -f "$$ACTUAL_CONF_FILE"; \
	systemctl daemon-reload; \
	echo "[uninstall] done (install.log preserved)"

clean:
	rm -f $(TARGET)

distclean: clean
	rm -f install.log
