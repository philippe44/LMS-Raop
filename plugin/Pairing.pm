package Plugins::RaopBridge::Pairing;

use strict;

use base qw(Slim::Plugin::Base);

use Data::Dumper;
use File::Spec::Functions;
use XML::Simple;
use Data::Plist::BinaryWriter;
use Data::Plist::BinaryReader;
use Date::Parse;
use Encode qw(decode encode);
use version;

eval "use CryptX";

use Slim::Utils::Prefs;
use Slim::Utils::Log;
use Slim::Networking::Async::HTTP;
use Slim::Utils::Misc qw(parseRevision);

my $prefs = preferences('plugin.raopbridge');
my $log   = logger('plugin.raopbridge');
my $use_async = 0;

=comment
eval {
	my (undef, $build) = parseRevision();
	my ($month, $day, $time, $year) = $build =~ /\S+ (\S+) (\S+) (\S+) \S+ (\S+)/;
	if (str2time("$day $month $year") < str2time('11 May 2019')) {
		require Plugins::RaopBridge::Async::HTTP;
		$log->error("LMS version is too old ($build), need own Aync::HTTP");
		Plugins::RaopBridge::Async::HTTP::init();
		$use_async = 0;
	}	
};	
=cut
require Plugins::RaopBridge::Async::HTTP;
Plugins::RaopBridge::Async::HTTP::init();
$use_async = 1;

sub displayPIN {
	my ($host) = @_;

	eval {
		require CryptX;
		require Crypt::SRP;
		require Crypt::Digest::SHA512;
		require Crypt::Ed25519;
		require Crypt::AuthEnc::GCM;
		Crypt::Digest::SHA512->import( qw(sha512) );
		Crypt::AuthEnc::GCM->import( qw(gcm_encrypt_authenticate gcm_decrypt_verify) );
	};
	
	if ($@) {
		$log->error("Cannot load crypto modules, please check your configuration", $@);
		return undef;
	}	
	
	my $request = HTTP::Request->new( POST => "$host/pair-pin-start" ); 
	$request->header( 'Connection' => 'Keep-Alive', 'Content-Type' => 'application/octet-stream' );
				
	my $session = $use_async ? Plugins::RaopBridge::Async::HTTP->new : Slim::Networking::Async::HTTP->new;
	$session->send_request( {
		request  => $request,
		onError  => sub { $log->error(Dumper(@_)); $log->error("Display PIN error") }, 
	} );
	
	return $session;
}

sub doPairing {
	my ($session, $cb, $host, $params) = @_;
	
	my $bplist = Data::Plist::BinaryWriter->new();
	my $content = $bplist->write( { 'method' => 'pin', 'user' => $params->{I} } );
	
	my $request = HTTP::Request->new( POST => "$host/pair-setup-pin" ); 
	$request->header( 'Connection' => 'keep-alive', 'Content-Type' => 'application/x-apple-binary-plist' );		
	$request->content( $content );
	
	$session->send_request( {
		request     => $request,
		
		onBody  => sub {
			my $content = shift->response->content;
			my $bplist = Data::Plist::BinaryReader->new;
			my $data = $bplist->open_string($content)->data;	
			
			doStep2($session, $cb, $host, { %$params, %$data } );
		},
		
		onError  => sub { 
			$log->error(Dumper(@_));
			$cb->(undef);
		},
	} );
}

sub doStep2 {
	my ($session, $cb, $host, $params) = @_;
	my $client = Crypt::SRP->new('RFC5054-2048bit', 'SHA1');
	
	(my $A, $params->{a}) = $client->client_compute_A(32);
	$params->{a_pub} = Crypt::Ed25519::eddsa_public_key($params->{a});
	
	$cb->(undef) if !$client->client_verify_B($params->{pk});

	$client->client_init($params->{I}, $params->{P}, $params->{salt});
	my $M1 = $client->client_compute_M1();
	$params->{K} = $client->get_secret_K();
	
	$log->info("<I>     : ", $params->{I});
	$log->info("<P>     : ", $params->{P});
	$log->info("<pk>    : ", unpack("H*", $params->{pk}));
	$log->info("<salt>  : ", unpack("H*", $params->{salt}));
	$log->info("<M1>    : ", unpack("H*", $M1));
	$log->info("<K>     : ", unpack("H*", $params->{K}));
	$log->info("<A>     : ", unpack("H*", $A));
	$log->info("<a>     : ", unpack("H*", $params->{a}));
	$log->info("<a_pub> : ", unpack("H*", $params->{a_pub}));

	my $bplist = Data::Plist::BinaryWriter->new();
	my $content = $bplist->write( { 'pk' => $A, 'proof' => $M1 } );
		
	my $request = HTTP::Request->new( POST => "$host/pair-setup-pin" );
	$request->header( 'Connection' => 'keep-alive', 'Content-Type' => 'application/x-apple-binary-plist' );		
	$request->content( $content );
	
	$session->send_request( {
		request => $request,
		
		onBody  => sub {
			my $content = shift->response->content;
			my $bplist = Data::Plist::BinaryReader->new;
			my $data = $bplist->open_string($content)->data;	
			
			doStep3($session, $cb, $host, { %$params, %$data } );
		},
		
		onError => sub { 
			$log->error(Dumper(@_));
			$cb->(undef);
		},
	} );
}	

sub doStep3 {
	my ($session, $cb, $host, $params) = @_;
	my $sha = Crypt::Digest::SHA512->new;

	$sha->add( encode('UTF-8','Pair-Setup-AES-Key') );
	$sha->add( $params->{K} );
	my $aes_key = substr($sha->digest, 0, 16);
	
	$sha->reset;
	$sha->add( encode('UTF-8','Pair-Setup-AES-IV') );
	$sha->add( $params->{K} );
	my $aes_iv = substr($sha->digest, 0, 16);
	substr($aes_iv, -1, 1) = pack('C', unpack('C', substr($aes_iv, -1, 1)) + 1);
	
	my ($epk, $tag) = gcm_encrypt_authenticate('AES', $aes_key, $aes_iv, '', $params->{a_pub});
	
	$log->info("<aes_key>     :", unpack("H*", $aes_key));
	$log->info("<aes_iv>      :", unpack("H*", $aes_iv));
	$log->info("<epk>         :", unpack("H*", $epk));
	$log->info("<tag>         :", unpack("H*", $tag));

	my $bplist = Data::Plist::BinaryWriter->new();
	my $content = $bplist->write( { 'epk' => $epk, 'authTag' => $tag } );
	
	my $request = HTTP::Request->new( POST => "$host/pair-setup-pin" );
	$request->header( 'Content-Type' => 'application/x-apple-binary-plist' );		
	$request->content( $content );
	
	$session->send_request( {
		request => $request,
		
		onBody  => sub {
			$cb->( $params->{a} );
		},
		
		onError => sub { 
			$log->error(Dumper(@_));
			$cb->( undef );
		},
	} );
}	


1;
