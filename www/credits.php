<!DOCTYPE html>
<html>
<?php
require_once('config.php');

error_reporting(E_ALL);
$fpp_version = "v" . getFPPVersion();

?>

<head>
<?php include 'common/menuHead.inc'; ?>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
<title>Credits</title>
<style>
.pl_title {
  font-size: larger;
}
h4, h3 {
  padding: 0;
  margin: 0;
}
a:active {
  color: none;
}
a:visited {
  color: blue;
}
.time {
  width: 100%;
}
.center {
  text-align: center;
}
</style>
</head>

<body>
<div id="bodyWrapper">
  <?php include 'menu.inc'; ?>
  <div style="margin:0 auto;"> <br />
    <fieldset style="padding: 10px; border: 2px solid #000;">
      <legend>Credits</legend>
      <div style="overflow: hidden; padding: 10px;">
    <div>
      <div id='credits'>
        <b>FPP Developed By:</b><br />
		<br />
        David Pitts<br />
        Tony Mace (MyKroFt)<br />
        Mathew Mrosko (Materdaddy)<br />
        Chris Pinkham (CaptainMurdoch)<br />
        Dan Kulp (dkulp)<br />
        Stephane Legargeant (ihbar)<br />
		<br />
        <hr width=300 />
		<br />
        <b>Video Tutorials by:</b><br />
		<br />
        Alan Dahl (bajadahl)<br />
		<br />
        <hr width=300 />
		<br />
        <b>3rd Party Libraries used by FPP for some Channel Outputs:</b><br />
		<br />
		<a href='https://github.com/jgarff/rpi_ws281x'>rpi_ws281x</a> by Jeremy Garff.  Used for driving WS281x pixels directly off the Pi's GPIO header.<br />
		<a href='https://github.com/hzeller/rpi-rgb-led-matrix'>rpi-rgb-led-matrix</a> by Henner Zeller.  Used for driving HUB75 panels directly off the Pi's GPIO header.<br />
		<a href='https://github.com/TMRh20/RF24'>RF24</a>. Used for driving nRF24L01 output for Komby.<br />
		<br />
        <hr width=180 />
		<br />
        Copyright &copy; 2013-2020
      </div>
    </div>
    </fieldset>
  </div>
  <?php include 'common/footer.inc'; ?>
</div>
</body>
</html>
