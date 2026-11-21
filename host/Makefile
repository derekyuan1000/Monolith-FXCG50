# PC harness for the Calculation screen: make -C host test
# Builds src/zupsui.cpp natively, runs the key scripts in scripts/ and writes
# screenshots to out/ (PNG if ImageMagick's convert is installed).
CXXFLAGS = -std=c++11 -O1 -g -Wall -fsanitize=address,undefined
OUT = out

$(OUT)/upshost: main.cpp ../src/zupsui.cpp ../src/upsui.h ../src/ups_fonts.h | $(OUT)
	$(CXX) $(CXXFLAGS) -I../src ../src/zupsui.cpp main.cpp -o $@

$(OUT):
	mkdir -p $@

test: $(OUT)/upshost
	@for s in scripts/*.txt; do n=$$(basename $$s .txt); echo "== $$n"; \
	  (cd $(OUT) && ./upshost ../$$s $${n}_) > $(OUT)/$$n.log || exit 1; \
	  grep -E 'FAIL|ok' $(OUT)/$$n.log || true; done
	@if command -v convert >/dev/null; then for f in $(OUT)/*.ppm; do convert $$f -filter point -resize 200% $${f%.ppm}.png && rm $$f; done; fi
	@if grep -q FAIL $(OUT)/*.log; then echo "some checks failed"; exit 1; else echo "all checks passed"; fi

clean:
	rm -rf $(OUT)

.PHONY: test clean
