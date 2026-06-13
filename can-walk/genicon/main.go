// 矢量绘制 CAN 固件升级工具图标，输出多尺寸 ICO（256/128/64/48/32/16）。
// 元素：渐变圆角背景 + 信号弧 + 芯片(带引脚) + 绿色升级箭头 + CAN 总线(节点)。
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"sort"
)

var (
	cBgTop = color.RGBA{0x0A, 0x25, 0x40, 0xFF} // 深蓝顶
	cBgBot = color.RGBA{0x16, 0x4D, 0x7A, 0xFF} // 亮蓝底
	cCyan  = color.RGBA{0x28, 0xCE, 0xF0, 0xFF} // 青色高光
	cCyanD = color.RGBA{0x0E, 0x7A, 0xA0, 0xFF} // 暗青
	cGreen = color.RGBA{0x33, 0xE0, 0x70, 0xFF} // 升级绿
	cChip  = color.RGBA{0x0C, 0x1A, 0x2A, 0xFF} // 芯片深色
	cBd    = color.RGBA{0x3A, 0x6E, 0xA2, 0xFF} // 芯片边框
	cPin   = color.RGBA{0x6A, 0x9A, 0xD0, 0xFF} // 引脚
)

func setPx(img *image.RGBA, x, y int, c color.RGBA) {
	b := img.Bounds()
	if x < b.Min.X || y < b.Min.Y || x >= b.Max.X || y >= b.Max.Y {
		return
	}
	img.SetRGBA(x, y, c)
}

func lerpC(c1, c2 color.RGBA, t float64) color.RGBA {
	mix := func(a, b uint8) uint8 { return uint8(float64(a) + (float64(b)-float64(a))*t) }
	return color.RGBA{mix(c1.R, c2.R), mix(c1.G, c2.G), mix(c1.B, c2.B), 255}
}

func fillRect(img *image.RGBA, c color.RGBA, x0, y0, x1, y1 int) {
	for y := y0; y < y1; y++ {
		for x := x0; x < x1; x++ {
			setPx(img, x, y, c)
		}
	}
}

func fillCircle(img *image.RGBA, c color.RGBA, cx, cy, r int) {
	for y := cy - r; y <= cy+r; y++ {
		for x := cx - r; x <= cx+r; x++ {
			dx, dy := x-cx, y-cy
			if dx*dx+dy*dy <= r*r {
				setPx(img, x, y, c)
			}
		}
	}
}

// inRoundRect 判断点是否在圆角矩形内
func inRR(x, y, x0, y0, x1, y1, r int) bool {
	if x < x0 || y < y0 || x >= x1 || y >= y1 {
		return false
	}
	if x0+r >= x1-r { // 圆角半径过大，退化为整矩形
		return true
	}
	check := func(px, py int) bool {
		dx, dy := x-px, y-py
		return dx*dx+dy*dy <= r*r
	}
	switch {
	case x < x0+r && y < y0+r:
		return check(x0+r, y0+r)
	case x >= x1-r && y < y0+r:
		return check(x1-r, y0+r)
	case x < x0+r && y >= y1-r:
		return check(x0+r, y1-r)
	case x >= x1-r && y >= y1-r:
		return check(x1-r, y1-r)
	}
	return true
}

func fillRoundGradient(img *image.RGBA, c1, c2 color.RGBA, x0, y0, x1, y1, r int) {
	h := y1 - y0
	if h <= 0 {
		h = 1
	}
	for y := y0; y < y1; y++ {
		c := lerpC(c1, c2, float64(y-y0)/float64(h))
		for x := x0; x < x1; x++ {
			if inRR(x, y, x0, y0, x1, y1, r) {
				setPx(img, x, y, c)
			}
		}
	}
}

func fillTriangle(img *image.RGBA, c color.RGBA, ax, ay, bx, by, cx, cy int) {
	minY, maxY := ay, ay
	for _, y := range []int{by, cy} {
		if y < minY {
			minY = y
		}
		if y > maxY {
			maxY = y
		}
	}
	for y := minY; y <= maxY; y++ {
		var xs []int
		edge := func(x0, y0, x1, y1 int) {
			if (y0 <= y && y < y1) || (y1 <= y && y < y0) {
				xs = append(xs, x0+(y-y0)*(x1-x0)/(y1-y0))
			}
		}
		edge(ax, ay, bx, by)
		edge(bx, by, cx, cy)
		edge(cx, cy, ax, ay)
		if len(xs) >= 2 {
			sort.Ints(xs)
			for x := xs[0]; x <= xs[len(xs)-1]; x++ {
				setPx(img, x, y, c)
			}
		}
	}
}

func thickLine(img *image.RGBA, c color.RGBA, x0, y0, x1, y1, t int) {
	if t < 1 {
		t = 1
	}
	dx, dy := x1-x0, y1-y0
	adx, ady := dx, dy
	if adx < 0 {
		adx = -adx
	}
	if ady < 0 {
		ady = -ady
	}
	steps := adx
	if ady > steps {
		steps = ady
	}
	half := t / 2
	if steps == 0 {
		fillRect(img, c, x0-half, y0-half, x0+half+1, y0+half+1)
		return
	}
	for i := 0; i <= steps; i++ {
		x := x0 + dx*i/steps
		y := y0 + dy*i/steps
		fillRect(img, c, x-half, y-half, x+half+1, y+half+1)
	}
}

// arc 画 a0..a1 度的弧（屏幕坐标，y 向下）
func arc(img *image.RGBA, c color.RGBA, cx, cy, r, a0, a1, t int) {
	steps := r
	if steps < 6 {
		steps = 6
	}
	px, py := -1, -1
	for i := 0; i <= steps; i++ {
		rad := (float64(a0) + float64(a1-a0)*float64(i)/float64(steps)) * math.Pi / 180
		x := cx + int(math.Round(float64(r)*math.Cos(rad)))
		y := cy + int(math.Round(float64(r)*math.Sin(rad)))
		if i > 0 {
			thickLine(img, c, px, py, x, y, t)
		}
		px, py = x, y
	}
}

func drawIcon(size int) *image.RGBA {
	img := image.NewRGBA(image.Rect(0, 0, size, size))
	sc := func(v float64) int { return int(math.Round(v * float64(size) / 256)) }

	// 背景圆角渐变
	fillRoundGradient(img, cBgTop, cBgBot, sc(14), sc(14), sc(242), sc(242), sc(32))

	// 信号波纹（顶部三道弧）
	for _, r := range []float64{13, 22, 31} {
		arc(img, cCyan, sc(128), sc(70), sc(r), 215, 325, sc(7))
	}

	// 芯片
	cx0, cy0, cx1, cy1 := sc(72), sc(84), sc(184), sc(176)
	fillRect(img, cChip, cx0, cy0, cx1, cy1)
	bw := sc(3)
	thickLine(img, cBd, cx0, cy0, cx1, cy0, bw)
	thickLine(img, cBd, cx0, cy1, cx1, cy1, bw)
	thickLine(img, cBd, cx0, cy0, cx0, cy1, bw)
	thickLine(img, cBd, cx1, cy0, cx1, cy1, bw)
	// 引脚
	pin := sc(9)
	for i := 0; i < 5; i++ {
		x := cx0 + (cx1-cx0)*(i+1)/6
		thickLine(img, cPin, x, cy0-pin, x, cy0, sc(3))
		thickLine(img, cPin, x, cy1, x, cy1+pin, sc(3))
	}
	for i := 0; i < 4; i++ {
		y := cy0 + (cy1-cy0)*(i+1)/5
		thickLine(img, cPin, cx0-pin, y, cx0, y, sc(3))
		thickLine(img, cPin, cx1, y, cx1+pin, y, sc(3))
	}

	// 绿色升级箭头
	fillTriangle(img, cGreen, sc(128), sc(104), sc(106), sc(128), sc(150), sc(128))
	fillRect(img, cGreen, sc(120), sc(120), sc(136), sc(160))

	// CAN 总线（底部）
	by := sc(204)
	thickLine(img, cCyanD, sc(128), sc(176), sc(128), by, sc(3))
	thickLine(img, cCyan, sc(42), by, sc(214), by, sc(5))
	for _, nx := range []float64{42, 128, 214} {
		fillCircle(img, cCyan, sc(nx), by, sc(7))
	}

	return img
}

func writeICO(path string, imgs []*image.RGBA) error {
	type entry struct {
		w, h int
		data []byte
	}
	var es []entry
	for _, im := range imgs {
		var b bytes.Buffer
		if err := png.Encode(&b, im); err != nil {
			return err
		}
		es = append(es, entry{im.Bounds().Dx(), im.Bounds().Dy(), b.Bytes()})
	}
	var buf bytes.Buffer
	binary.Write(&buf, binary.LittleEndian, uint16(0))       // reserved
	binary.Write(&buf, binary.LittleEndian, uint16(1))       // type = ICO
	binary.Write(&buf, binary.LittleEndian, uint16(len(es))) // count
	offset := 6 + 16*len(es)
	for _, e := range es {
		w, h := e.w, e.h
		if w >= 256 {
			w = 0
		} // 256 存为 0
		if h >= 256 {
			h = 0
		}
		buf.Write([]byte{byte(w), byte(h), 0, 0})
		binary.Write(&buf, binary.LittleEndian, uint16(1))           // planes
		binary.Write(&buf, binary.LittleEndian, uint16(32))          // bpp
		binary.Write(&buf, binary.LittleEndian, uint32(len(e.data))) // size
		binary.Write(&buf, binary.LittleEndian, uint32(offset))      // offset
		offset += len(e.data)
	}
	for _, e := range es {
		buf.Write(e.data)
	}
	return os.WriteFile(path, buf.Bytes(), 0644)
}

func main() {
	sizes := []int{256, 128, 64, 48, 32, 16}
	var imgs []*image.RGBA
	for _, s := range sizes {
		imgs = append(imgs, drawIcon(s))
	}
	out := "icon.ico"
	if len(os.Args) > 1 {
		out = os.Args[1]
	}
	if err := writeICO(out, imgs); err != nil {
		fmt.Println("error:", err)
		os.Exit(1)
	}
	fmt.Printf("wrote %s (%d sizes)\n", out, len(sizes))
	if f, err := os.Create("preview.png"); err == nil {
		png.Encode(f, imgs[0])
		f.Close()
		fmt.Println("wrote preview.png (256x256)")
	}
}
