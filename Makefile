# ========= Proyecto SO - Comunicación sincronizada =========
# Ubicación: ESTE Makefile va en la raíz del repo (fuera de src/)
# Código fuente: src/*.c   |  Encabezados: src/shared.h
# Binarios: bin/           |  Objetos: build/

# --- Config ---
CC      := gcc
SRCDIR  := src
BINDIR  := bin
OBJDIR  := build

CFLAGS  := -std=c99 -O2 -Wall -Wextra -I$(SRCDIR)
LDFLAGS :=

BINARIES := $(BINDIR)/inicializador $(BINDIR)/emisor $(BINDIR)/receptor $(BINDIR)/finalizador
OBJS     := $(OBJDIR)/Inicializador.o $(OBJDIR)/Emisor.o $(OBJDIR)/Receptor.o $(OBJDIR)/finalizador.o
HEADERS  := $(SRCDIR)/shared.h

# --- Phony ---
.PHONY: all clean distclean run dirs

# --- Entradas principales ---
all: dirs $(BINARIES)

dirs:
	@mkdir -p $(BINDIR) $(OBJDIR)

# --- Enlazado de binarios ---
$(BINDIR)/inicializador: $(OBJDIR)/Inicializador.o | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BINDIR)/emisor: $(OBJDIR)/Emisor.o | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BINDIR)/receptor: $(OBJDIR)/Receptor.o | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BINDIR)/finalizador: $(OBJDIR)/finalizador.o | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- Compilación a .o (desde src/ a build/) ---
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Ejecución de ejemplo  ---
run: all
	@echo "== Ejemplo =="
	@echo "1) Inicializando SHM/SEMs (ID=123, buffer=16, XOR=42, fuente=src/texto_fuente.txt)"
	$(BINDIR)/inicializador 123 16 42 $(SRCDIR)/texto_fuente.txt || true
	@echo "2) Lanzando receptores (auto=1, XOR=42) escribiendo colaborativamente en salida.txt"
	$(BINDIR)/receptor 123 1 42 salida.txt & \
	$(BINDIR)/receptor 123 1 42 salida.txt & \
	sleep 1; \
	echo "3) Lanzando emisor (auto=1, XOR=42)"; \
	$(BINDIR)/emisor 123 1 42; \
	echo "4) Finalizando"; \
	$(BINDIR)/finalizador 123; \
	wait || true
	@echo "== Fin =="

# --- Limpiezas ---
clean:
	@rm -f $(OBJS) $(BINARIES)

distclean: clean
	@rm -rf $(OBJDIR) $(BINDIR)
