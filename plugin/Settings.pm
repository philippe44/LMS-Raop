package Plugins::RaopBridge::Settings;

use strict;

use File::Spec::Functions;
use LWP::Simple;
use base qw(Slim::Web::Settings);
use XML::Simple;
use Data::Dumper;
use Slim::Utils::PluginManager;
use Slim::Utils::Prefs;
use Slim::Utils::Log;


my $prefs = preferences('plugin.raopbridge');
my $log   = logger('plugin.raopbridge');
my @xmlmainskip = qw(interface);
my @xmlmain = ( @xmlmainskip, qw(log_limit ports) );
my @xmldevice = qw(name mac codecs enabled remove_timeout send_metadata send_coverart player_volume idle_timeout read_ahead encryption server volume_feedback volume_mode volume_mapping mute_on_pause alac_encode volume_trigger prevent_playback);
my @prefs_bool  = qw(autorun logging autosave eraselog useLMSsocket);
my @prefs_other = qw(output bin debugs opts);

my $session; # this can be a global, only one instance can run

sub name { 'PLUGIN_RAOPBRIDGE' }

sub page { 'plugins/RaopBridge/settings/basic.html' }
	
sub handler {
	my ($class, $client, $params, $callback, @args) = @_;
	my $process;
		
	require Plugins::RaopBridge::Squeeze2raop;
	require Plugins::RaopBridge::Plugin;
			
	if ( $params->{ 'delconfig' } ) {
		my $conf = Plugins::RaopBridge::Squeeze2raop->configFile($class);
		
		$process = { cb => undef };
		unlink $conf;							
		$log->info("deleting configuration $conf");
	} elsif ( $params->{ 'genconfig' } ) {
		$log->info("generating configuration ", Plugins::RaopBridge::Squeeze2raop->configFile($class));
		$process = { cb => \&genConfig };
	} elsif ( $params->{ 'cleanlog' } ) {
		my $logfile = Plugins::RaopBridge::Squeeze2raop->logFile($class);

		open my $fh, ">", $logfile;
		print $fh;
		close $fh;
	} elsif ($params->{ 'restart' }) {
		Plugins::RaopBridge::Squeeze2raop->stop;
		waitEndHandler(undef, $class, $client, $params, $callback, 30, @args);
	} elsif ( $params->{'pairdevice'} ) {
		my $xmlconfig = readconfig($class, KeyAttr => 'device');
		my ($device) = \grep { $_->{udn} eq $params->{'pairdevice'} } @{$xmlconfig->{'device'}};
		my ($host) = ($$device->{'credentials'} =~ /.*@(.+)/);
			
		if ( $params->{pincode} && $session) {
			my $data = { 'I' => $$device->{mac}, 'P' => $params->{ 'pincode' } };
			my $udn = $params->{'pairdevice'};
			my $writeXML =	sub {
					my $secret = shift;
						
					if ( defined $secret ) { 
						$log->info("$udn pairing success: ", unpack("H*", $secret)); 
						$$device->{credentials} = unpack("H*", $secret) . '@' . $host;
					}			
					else { 
						$$device->{credentials} = '@' . $host;
						$log->error("$udn pairing failed");
					}	
					
					XMLout(	$xmlconfig, RootName => "squeeze2raop", NoSort => 1, NoAttr => 1, 
								OutputFile => Plugins::RaopBridge::Squeeze2raop->configFile($class)); 
					
					waitEndHandler( { cb => undef }, $class, $client, $params, $callback, 30, @args);								
				};
				
			$process = { cb => sub { Plugins::RaopBridge::Pairing::doPairing( $session, $writeXML, "http://$host", $data ) } };
			delete $params->{'pairdevice'};
			
			$log->info("I=$data->{I} P=$data->{P}");
		} else {
			$log->debug("Launching pincode display for host:$host udn:$params->{'pairdevice'}");
			$session = Plugins::RaopBridge::Pairing::displayPIN( "http://$host" );
			delete $params->{'pairdevice'} if !$session;
		}
	} elsif ( $params->{'saveSettings'} ) {
		my $update;
		
		$log->debug("save settings required");
				
		for my $param (@prefs_bool) {
			my $val = $params->{ $param } ? 1 : 0;
			
			if ($val != $prefs->get($param)) {
				$prefs->set($param, $val);
				$update = 1;
			}
		}
		
		# check that the config file name has not changed first
		for my $param (@prefs_other) {
			if ($params->{ $param } ne $prefs->get($param)) {
			
				$prefs->set($param, $params->{ $param });
				$update = 1;
			}
		}
		
		my $xmlconfig = readconfig($class, KeyAttr => 'device');
		
		if ($params->{ 'configfile' } ne $prefs->get('configfile')) {
			$prefs->set('configfile', $params->{ 'configfile' });
			if (-e Plugins::RaopBridge::Squeeze2raop->configFile($class)) {
				$update = 0;
				undef $xmlconfig;
			} 
		}	
				
		# get XML player configuration if current device has changed in the list
		if ($xmlconfig && ($params->{'seldevice'} eq $params->{'prevseldevice'})) {
						
			for my $p (@xmlmain) {
				next if !defined $params->{ $p } && grep($_ eq $p, @xmlmainskip);
												
				if (!defined $params->{ $p }) {
					$xmlconfig->{ $p } = 0;
				} elsif ($params->{ $p } eq 'on') {
					$xmlconfig->{ $p } = 1;
				} elsif ($params->{ $p } eq '') {
					delete $xmlconfig->{ $p };
				} else {
					$xmlconfig->{ $p } = $params->{ $p };
				}	
			}
			
			$log->info("current: ", $params->{'seldevice'}, "previous: ", $params->{'prevseldevice'});
			
			if ($params->{'seldevice'} eq '.common.') {
				#save common parameters
				for my $p (@xmldevice) {
					if ($params->{ $p } eq '') {
						delete $xmlconfig->{ common }->{ $p };
					} else {
						$xmlconfig->{ common }->{ $p } = $params->{ $p };
					}
				}	
			} elsif ( $params->{'deldevice'} ) {
				#delete current device	
				$log->info(@{$xmlconfig->{'device'}});
				@{$xmlconfig->{'device'}} = grep $_->{'udn'} ne $params->{'seldevice'}, @{$xmlconfig->{'device'}};
				$params->{'seldevice'} = '.common.';
			} else {
				# save player specific parameters
				$params->{'devices'} = \@{$xmlconfig->{'device'}};
				my $device = findUDN($params->{'seldevice'}, $params->{'devices'});
					
				for my $p (@xmldevice) {
					if ($params->{ $p } eq '') {
						delete $device->{ $p };
					} else {
						$device->{ $p } = $params->{ $p };
					}
				}	
			}	
			
			# get enabled status for all device, except the selected one (if any)
			foreach my $device (@{$xmlconfig->{'device'}}) {
				if ($device->{'udn'} ne $params->{'seldevice'}) {
					my $enabled = $params->{ 'enabled.'.$device->{ 'udn' } };
					$device->{'enabled'} = defined $enabled ? $enabled : 0;
				}	
			}
			
			$log->info("writing XML config");
			$log->debug(Dumper($xmlconfig));
			
			$update = 1;
		}	
		
		if ($update) {
			my $writeXML = sub {
				my $conf = Plugins::RaopBridge::Squeeze2raop->configFile($class);
				
				return if !$xmlconfig;
				$log->debug("write file now");
				XMLout(	$xmlconfig, RootName => "squeeze2raop", NoSort => 1, NoAttr => 1, OutputFile => $conf );
			};
			
			$process = { cb => $writeXML, handler => 1 };
		}	
	}

	# something has been updated, XML array is up-to-date anyway, but need to write it
	if ($process) {
		$log->debug("full processing");
		Plugins::RaopBridge::Squeeze2raop->stop;
		waitEndHandler($process, $class, $client, $params, $callback, 30, @args);
	} else {
		# just re-read config file and update page
		$log->debug("not updating");
		$class->handler2($client, $params, $callback, @args);		  
	}

	return undef;
}

sub waitEndHandler	{
	my ($process, $class, $client, $params, $callback, $wait, @args) = @_;
	my $page;
	
	if ( Plugins::RaopBridge::Squeeze2raop->alive() ) {
		$log->debug('Waiting for squeeze2raop to end');
		$wait--;
		if ($wait) {
			Slim::Utils::Timers::setTimer($class, Time::HiRes::time() + 1, sub {
				waitEndHandler($process, $class, $client, $params, $callback, $wait, @args); });
		}		
	} elsif ( defined $process->{cb} ) {
		$log->debug("helper stopped, processing with callback");
		$process->{cb}->($class, $client, $params, $callback, @args);
		$page = $process->{handler};
	} else {
		$page = 1;
	}
	
	if ( $page ) {
		$log->debug("updating page");
		Plugins::RaopBridge::Squeeze2raop->start if $prefs->get('autorun');
		$class->handler2($client, $params, $callback, @args);		  
	}
}

sub genConfig {
	my ($class, $client, $params, $callback, @args) = @_;
	my $conf = Plugins::RaopBridge::Squeeze2raop->configFile($class);
	
	$log->debug("lauching helper to build $conf");
	Plugins::RaopBridge::Squeeze2raop->start( "-i", $conf );
	waitEndHandler({ cb => undef}, $class, $client, $params, $callback, 120, @args);
}	

sub handler2 {
	my ($class, $client, $params, $callback, @args) = @_;

	if ($prefs->get('autorun')) {
		$params->{'running'}  = Plugins::RaopBridge::Squeeze2raop->alive;
	} else {
		$params->{'running'} = 0;
	}

	$params->{'binary'}   = Plugins::RaopBridge::Squeeze2raop->bin;
	$params->{'binaries'} = [ Plugins::RaopBridge::Squeeze2raop->binaries ];
	for my $param (@prefs_bool, @prefs_other, qw(configfile)) {
		$params->{ $param } = $prefs->get($param);
	}
	
	$params->{'configpath'} = Slim::Utils::OSDetect::dirsFor('prefs');
	$params->{'arch'} = Slim::Utils::OSDetect::OS();
	
	my $xmlconfig = readconfig($class, KeyAttr => 'device');
		
	#load XML parameters from config file	
	if ($xmlconfig) {
	
		$params->{'devices'} = \@{$xmlconfig->{'device'}};
		unshift(@{$params->{'devices'}}, {'name' => '[common parameters]', 'udn' => '.common.'});
		
		$log->debug(Dumper($params->{'devices'}));
				
		#read global parameters
		for my $p (@xmlmain) {
			$params->{ $p } = $xmlconfig->{ $p };
			$log->debug("reading: ", $p, " ", $xmlconfig->{ $p });
		}
		
		# read either common parameters or device-specific
		if (!defined $params->{'seldevice'} or ($params->{'seldevice'} eq '.common.')) {
			$params->{'seldevice'} = '.common.';
			
			for my $p (@xmldevice) {
				$params->{ $p } = $xmlconfig->{common}->{ $p };
			}	
		} else {
			my $device = findUDN($params->{'seldevice'}, $params->{'devices'});
			
			for my $p (@xmldevice) {
				$params->{ $p } = $device->{ $p };
			}
		}
		$params->{'prevseldevice'} = $params->{'seldevice'};
		$params->{'xmlparams'} = 1;
		
	} else {
	
		$params->{'xmlparams'} = 0;
	}
	
	$callback->($client, $params, $class->SUPER::handler($client, $params), @args);
}

sub mergeprofile{
	my ($p1, $p2) = @_;
	
	foreach my $m (keys %$p2) {
		$p1->{ $m } = $p2-> { $m };
	}	
}


sub findUDN {
	my $udn = shift(@_);
	my $listpar = shift(@_);
	my @list = @{$listpar};
	
	while (@list) {
		my $p = pop @list;
		if ($p->{ 'udn' } eq $udn) { return $p; }
	}
	return undef;
}


sub readconfig {
	my ($class,@args) = @_;
	my $ret;
	
	my $file = Plugins::RaopBridge::Squeeze2raop->configFile($class);
	if (-e $file) {
		$ret = XMLin($file, ForceArray => ['device'], KeepRoot => 0, NoAttr => 1, @args);
	}	
	return $ret;
}

1;
