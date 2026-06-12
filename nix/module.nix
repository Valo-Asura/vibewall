{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.vibewallREzero;
  package = pkgs.callPackage ./package.nix { };
in
{
  options.programs.vibewallREzero = {
    enable = lib.mkEnableOption "vibewallREzero native wallpaper selector";
    package = lib.mkOption {
      type = lib.types.package;
      default = package;
      description = "vibewallREzero package.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    systemd.user.services.vibewallrezero-daemon = {
      description = "vibewallREzero wallpaper daemon";
      after = [ "graphical-session.target" ];
      partOf = [ "graphical-session.target" ];
      wantedBy = [ "graphical-session.target" ];
      serviceConfig = {
        ExecStart = "${cfg.package}/bin/vibewall-daemon";
        Restart = "on-failure";
        RestartSec = 2;
      };
    };
  };
}
