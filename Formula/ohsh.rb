class Ohsh < Formula
  desc "Ohsh - custom shell by GabeX"
  homepage "https://github.com/gabex47/homebrew-ohsh"
  url "https://github.com/gabex47/homebrew-ohsh/archive/refs/tags/v1.0.1.tar.gz"
  sha256 "14b14f45e53158b5f07a10fdf8784e4c9a86b8f0a666642bdad04a39637063c2"
  license "MIT"

  depends_on "make"

  def install
    system "make", "build"
    bin.install "ohsh"
  end

  test do
    system "#{bin}/ohsh", "--version"
  end
end