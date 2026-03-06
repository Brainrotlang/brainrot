{
  description = "Brainrot programming language interpreter - Sigma Edition";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux"; 
      pkgs = import nixpkgs { inherit system; };
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "brainrot";
        version = "1.0.0";
        src = ./.;

        nativeBuildInputs = with pkgs; [ 
          flex 
          bison 
          gcc 
        ];

        buildInputs = with pkgs; [ 
          # On Nix, flex provides the headers and libs (libfl) 
          # needed for the -lfl flag.
          flex 
        ];

        # Ensure the Makefile uses the correct paths
        buildPhase = ''
          make all
        '';

        installPhase = ''
          mkdir -p $out/bin
          cp brainrot $out/bin/
        '';
      };

      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [ 
          flex 
          bison 
          gcc 
          python3 
          python3Packages.pytest
          valgrind 
          clang-tools
        ];
      };
    };
}
