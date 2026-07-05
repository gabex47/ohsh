class Ohsh < Formula
  desc "Human-first shell language"
  homepage "https://github.com/gabex47/ohsh"
  license :cannot_represent
  head "https://github.com/gabex47/ohsh.git", branch: "main"

  def install
    system "make", "CC=#{ENV.cc}", "build"
    bin.install "ohsh"
  end

  test do
    output = shell_output("printf 'help\\nexit\\n' | #{bin}/ohsh")
    assert_match "ohsh", output.downcase
  end
end
