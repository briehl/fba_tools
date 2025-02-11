package fba_tools::fba_toolsServer;


use Data::Dumper;
use Moose;
use POSIX;
use JSON;
use Bio::KBase::Log;
use Config::Simple;
my $get_time = sub { time, 0 };
eval {
    require Time::HiRes;
    $get_time = sub { Time::HiRes::gettimeofday };
};

use Bio::KBase::AuthToken;

extends 'RPC::Any::Server::JSONRPC::PSGI';

has 'instance_dispatch' => (is => 'ro', isa => 'HashRef');
has 'user_auth' => (is => 'ro', isa => 'UserAuth');
has 'valid_methods' => (is => 'ro', isa => 'HashRef', lazy => 1,
			builder => '_build_valid_methods');
has 'loggers' => (is => 'ro', required => 1, builder => '_build_loggers');
has 'config' => (is => 'ro', required => 1, builder => '_build_config');
has 'local_headers' => (is => 'ro', isa => 'HashRef');

our $CallContext;

our %return_counts = (
        'build_metabolic_model' => 1,
        'build_multiple_metabolic_models' => 1,
        'gapfill_metabolic_model' => 1,
        'run_flux_balance_analysis' => 1,
        'compare_fba_solutions' => 1,
        'propagate_model_to_new_genome' => 1,
        'simulate_growth_on_phenotype_data' => 1,
        'merge_metabolic_models_into_community_model' => 1,
        'view_flux_network' => 1,
        'compare_flux_with_expression' => 1,
        'check_model_mass_balance' => 1,
        'predict_auxotrophy' => 1,
        'compare_models' => 1,
        'edit_metabolic_model' => 1,
        'edit_media' => 1,
        'excel_file_to_model' => 1,
        'sbml_file_to_model' => 1,
        'tsv_file_to_model' => 1,
        'model_to_excel_file' => 1,
        'model_to_sbml_file' => 1,
        'model_to_tsv_file' => 1,
        'export_model_as_excel_file' => 1,
        'export_model_as_tsv_file' => 1,
        'export_model_as_sbml_file' => 1,
        'fba_to_excel_file' => 1,
        'fba_to_tsv_file' => 1,
        'export_fba_as_excel_file' => 1,
        'export_fba_as_tsv_file' => 1,
        'tsv_file_to_media' => 1,
        'excel_file_to_media' => 1,
        'media_to_tsv_file' => 1,
        'media_to_excel_file' => 1,
        'export_media_as_excel_file' => 1,
        'export_media_as_tsv_file' => 1,
        'tsv_file_to_phenotype_set' => 1,
        'phenotype_set_to_tsv_file' => 1,
        'export_phenotype_set_as_tsv_file' => 1,
        'phenotype_simulation_set_to_excel_file' => 1,
        'phenotype_simulation_set_to_tsv_file' => 1,
        'export_phenotype_simulation_set_as_excel_file' => 1,
        'export_phenotype_simulation_set_as_tsv_file' => 1,
        'bulk_export_objects' => 1,
        'status' => 1,
);

our %method_authentication = (
        'build_metabolic_model' => 'required',
        'build_multiple_metabolic_models' => 'required',
        'gapfill_metabolic_model' => 'required',
        'run_flux_balance_analysis' => 'required',
        'compare_fba_solutions' => 'required',
        'propagate_model_to_new_genome' => 'required',
        'simulate_growth_on_phenotype_data' => 'required',
        'merge_metabolic_models_into_community_model' => 'required',
        'view_flux_network' => 'required',
        'compare_flux_with_expression' => 'required',
        'check_model_mass_balance' => 'required',
        'predict_auxotrophy' => 'required',
        'compare_models' => 'required',
        'edit_metabolic_model' => 'required',
        'edit_media' => 'required',
        'excel_file_to_model' => 'required',
        'sbml_file_to_model' => 'required',
        'tsv_file_to_model' => 'required',
        'model_to_excel_file' => 'required',
        'model_to_sbml_file' => 'required',
        'model_to_tsv_file' => 'required',
        'export_model_as_excel_file' => 'required',
        'export_model_as_tsv_file' => 'required',
        'export_model_as_sbml_file' => 'required',
        'fba_to_excel_file' => 'required',
        'fba_to_tsv_file' => 'required',
        'export_fba_as_excel_file' => 'required',
        'export_fba_as_tsv_file' => 'required',
        'tsv_file_to_media' => 'required',
        'excel_file_to_media' => 'required',
        'media_to_tsv_file' => 'required',
        'media_to_excel_file' => 'required',
        'export_media_as_excel_file' => 'required',
        'export_media_as_tsv_file' => 'required',
        'tsv_file_to_phenotype_set' => 'required',
        'phenotype_set_to_tsv_file' => 'required',
        'export_phenotype_set_as_tsv_file' => 'required',
        'phenotype_simulation_set_to_excel_file' => 'required',
        'phenotype_simulation_set_to_tsv_file' => 'required',
        'export_phenotype_simulation_set_as_excel_file' => 'required',
        'export_phenotype_simulation_set_as_tsv_file' => 'required',
        'bulk_export_objects' => 'required',
);

sub _build_valid_methods
{
    my($self) = @_;
    my $methods = {
        'build_metabolic_model' => 1,
        'build_multiple_metabolic_models' => 1,
        'gapfill_metabolic_model' => 1,
        'run_flux_balance_analysis' => 1,
        'compare_fba_solutions' => 1,
        'propagate_model_to_new_genome' => 1,
        'simulate_growth_on_phenotype_data' => 1,
        'merge_metabolic_models_into_community_model' => 1,
        'view_flux_network' => 1,
        'compare_flux_with_expression' => 1,
        'check_model_mass_balance' => 1,
        'predict_auxotrophy' => 1,
        'compare_models' => 1,
        'edit_metabolic_model' => 1,
        'edit_media' => 1,
        'excel_file_to_model' => 1,
        'sbml_file_to_model' => 1,
        'tsv_file_to_model' => 1,
        'model_to_excel_file' => 1,
        'model_to_sbml_file' => 1,
        'model_to_tsv_file' => 1,
        'export_model_as_excel_file' => 1,
        'export_model_as_tsv_file' => 1,
        'export_model_as_sbml_file' => 1,
        'fba_to_excel_file' => 1,
        'fba_to_tsv_file' => 1,
        'export_fba_as_excel_file' => 1,
        'export_fba_as_tsv_file' => 1,
        'tsv_file_to_media' => 1,
        'excel_file_to_media' => 1,
        'media_to_tsv_file' => 1,
        'media_to_excel_file' => 1,
        'export_media_as_excel_file' => 1,
        'export_media_as_tsv_file' => 1,
        'tsv_file_to_phenotype_set' => 1,
        'phenotype_set_to_tsv_file' => 1,
        'export_phenotype_set_as_tsv_file' => 1,
        'phenotype_simulation_set_to_excel_file' => 1,
        'phenotype_simulation_set_to_tsv_file' => 1,
        'export_phenotype_simulation_set_as_excel_file' => 1,
        'export_phenotype_simulation_set_as_tsv_file' => 1,
        'bulk_export_objects' => 1,
        'status' => 1,
    };
    return $methods;
}

my $DEPLOY = 'KB_DEPLOYMENT_CONFIG';
my $SERVICE = 'KB_SERVICE_NAME';

sub get_config_file
{
    my ($self) = @_;
    if(!defined $ENV{$DEPLOY}) {
        return undef;
    }
    return $ENV{$DEPLOY};
}

sub get_service_name
{
    my ($self) = @_;
    if(!defined $ENV{$SERVICE}) {
        return 'fba_tools';
    }
    return $ENV{$SERVICE};
}

sub _build_config
{
    my ($self) = @_;
    my $sn = $self->get_service_name();
    my $cf = $self->get_config_file();
    if (!($cf)) {
        return {};
    }
    my $cfg = new Config::Simple($cf);
    my $cfgdict = $cfg->get_block($sn);
    if (!($cfgdict)) {
        return {};
    }
    return $cfgdict;
}

sub logcallback
{
    my ($self) = @_;
    $self->loggers()->{serverlog}->set_log_file(
        $self->{loggers}->{userlog}->get_log_file());
}

sub log
{
    my ($self, $level, $context, $message, $tag) = @_;
    my $user = defined($context->user_id()) ? $context->user_id(): undef; 
    $self->loggers()->{serverlog}->log_message($level, $message, $user, 
        $context->module(), $context->method(), $context->call_id(),
        $context->client_ip(), $tag);
}

sub _build_loggers
{
    my ($self) = @_;
    my $submod = $self->get_service_name();
    my $loggers = {};
    my $callback = sub {$self->logcallback();};
    $loggers->{userlog} = Bio::KBase::Log->new(
            $submod, {}, {ip_address => 1, authuser => 1, module => 1,
            method => 1, call_id => 1, changecallback => $callback,
	    tag => 1,
            config => $self->get_config_file()});
    $loggers->{serverlog} = Bio::KBase::Log->new(
            $submod, {}, {ip_address => 1, authuser => 1, module => 1,
            method => 1, call_id => 1,
	    tag => 1,
            logfile => $loggers->{userlog}->get_log_file()});
    $loggers->{serverlog}->set_log_level(6);
    return $loggers;
}

#override of RPC::Any::Server
sub handle_error {
    my ($self, $error) = @_;
    
    unless (ref($error) eq 'HASH' ||
           (blessed $error and $error->isa('RPC::Any::Exception'))) {
        $error = RPC::Any::Exception::PerlError->new(message => $error);
    }
    my $output;
    eval {
        my $encoded_error = $self->encode_output_from_exception($error);
        $output = $self->produce_output($encoded_error);
    };
    
    return $output if $output;
    
    die "$error\n\nAlso, an error was encountered while trying to send"
        . " this error: $@\n";
}

#override of RPC::Any::JSONRPC
sub encode_output_from_exception {
    my ($self, $exception) = @_;
    my %error_params;
    if (ref($exception) eq 'HASH') {
        %error_params = %{$exception};
        if(defined($error_params{context})) {
            my @errlines;
            $errlines[0] = $error_params{message};
            push @errlines, split("\n", $error_params{data});
            $self->log($Bio::KBase::Log::ERR, $error_params{context}, \@errlines);
            delete $error_params{context};
        }
    } else {
        %error_params = (
            message => $exception->message,
            code    => $exception->code,
        );
    }
    my $json_error;
    if ($self->_last_call) {
        $json_error = $self->_last_call->return_error(%error_params);
    }
    # Default to default_version. This happens when we throw an exception
    # before inbound parsing is complete.
    else {
        $json_error = $self->_default_error(%error_params);
    }
    return $self->encode_output_from_object($json_error);
}

sub trim {
    my ($str) = @_;
    if (!(defined $str)) {
        return $str;
    }
    $str =~ s/^\s+|\s+$//g;
    return $str;
}

sub getIPAddress {
    my ($self) = @_;
    my $xFF = trim($self->_plack_req_header("X-Forwarded-For"));
    my $realIP = trim($self->_plack_req_header("X-Real-IP"));
    my $nh = $self->config->{"dont_trust_x_ip_headers"};
    my $trustXHeaders = !(defined $nh) || $nh ne "true";

    if ($trustXHeaders) {
        if ($xFF) {
            my @tmp = split(",", $xFF);
            return trim($tmp[0]);
        }
        if ($realIP) {
            return $realIP;
        }
    }
    if (defined($self->_plack_req)) {
        return $self->_plack_req->address;
    }
    return "localhost";
}

sub call_method {
    my ($self, $data, $method_info) = @_;

    my ($module, $method, $modname) = @$method_info{qw(module method modname)};
    
    my $ctx = fba_tools::fba_toolsServerContext->new($self->{loggers}->{userlog},
                           client_ip => $self->getIPAddress());
    $ctx->module($modname);
    $ctx->method($method);
    $ctx->call_id($self->{_last_call}->{id});
    
    my $args = $data->{arguments};
    my $prov_action = {'service' => $modname, 'method' => $method, 'method_params' => $args};
    $ctx->provenance([$prov_action]);
{
    # Service fba_tools requires authentication.

    my $method_auth = $method_authentication{$method};
    $ctx->authenticated(0);
    if ($method_auth eq 'none')
    {
	# No authentication required here. Move along.
    }
    else
    {
	my $token = $self->_plack_req_header("Authorization");

	if (!$token && $method_auth eq 'required')
	{
	    $self->exception('PerlError', "Authentication required for fba_tools but no authentication header was passed");
	}

	my $auth_token;
        if ($self->config->{'auth-service-url'})
        {
            $auth_token = Bio::KBase::AuthToken->new(token => $token, ignore_authrc => 1, auth_svc=>$self->config->{'auth-service-url'});
        } else {
            $auth_token = Bio::KBase::AuthToken->new(token => $token, ignore_authrc => 1);
        }

	my $valid = $auth_token->validate();
	# Only throw an exception if authentication was required and it fails
	if ($method_auth eq 'required' && !$valid)
	{
	    $self->exception('PerlError', "Token validation failed: " . $auth_token->error_message);
	} elsif ($valid) {
	    $ctx->authenticated(1);
	    $ctx->user_id($auth_token->user_id);
	    $ctx->token( $token);
	}
    }
}
    my $new_isa = $self->get_package_isa($module);
    no strict 'refs';
    local @{"${module}::ISA"} = @$new_isa;
    local $CallContext = $ctx;
    my @result;
    {
        # 
        # Process tag and metadata information if present.
        #
        my $tag = $self->_plack_req_header("Kbrpc-Tag");
        if (!$tag)
        {
            my ($t, $us) = &$get_time();
            $us = sprintf("%06d", $us);
            my $ts = strftime("%Y-%m-%dT%H:%M:%S.${us}Z", gmtime $t);
            $tag = "S:$self->{hostname}:$$:$ts";
        }
        local $ENV{KBRPC_TAG} = $tag;
        my $kb_metadata = $self->_plack_req_header("Kbrpc-Metadata");
        my $kb_errordest = $self->_plack_req_header("Kbrpc-Errordest");
        local $ENV{KBRPC_METADATA} = $kb_metadata if $kb_metadata;
        local $ENV{KBRPC_ERROR_DEST} = $kb_errordest if $kb_errordest;

        my $stderr = fba_tools::fba_toolsServerStderrWrapper->new($ctx, $get_time);
        $ctx->stderr($stderr);

        my $xFF = $self->_plack_req_header("X-Forwarded-For");
        if ($xFF) {
            $self->log($Bio::KBase::Log::INFO, $ctx,
                "X-Forwarded-For: " . $xFF, $tag);
        }
	
        my $err;
        eval {
            $self->log($Bio::KBase::Log::INFO, $ctx, "start method", $tag);
            local $SIG{__WARN__} = sub {
                my($msg) = @_;
                $stderr->log($msg);
                print STDERR $msg;
            };

            @result = $module->$method(@{ $data->{arguments} });
            $self->log($Bio::KBase::Log::INFO, $ctx, "end method", $tag);
        };

        if ($@)
        {
            my $err = $@;
            $stderr->log($err);
            $ctx->stderr(undef);
            undef $stderr;
            $self->log($Bio::KBase::Log::INFO, $ctx, "fail method", $tag);
            my $nicerr;
            if(ref($err) eq "Bio::KBase::Exceptions::KBaseException") {
                $nicerr = {code => -32603, # perl error from RPC::Any::Exception
                           message => $err->error,
                           data => $err->trace->as_string,
                           context => $ctx
                           };
            } else {
                my $str = "$err";
                $str =~ s/Bio::KBase::CDMI::Service::call_method.*//s; # is this still necessary? not sure
                my $msg = $str;
                $msg =~ s/ at [^\s]+.pm line \d+.\n$//;
                $nicerr =  {code => -32603, # perl error from RPC::Any::Exception
                            message => $msg,
                            data => $str,
                            context => $ctx
                            };
            }
            die $nicerr;
        }
        $ctx->stderr(undef);
        undef $stderr;
    }
    my $result;
    if ($return_counts{$method} == 1)
    {
        $result = [[$result[0]]];
    }
    else
    {
        $result = \@result;
    }
    return $result;
}

sub _plack_req_header {
    my ($self, $header_name) = @_;
    if (defined($self->local_headers)) {
        return $self->local_headers->{$header_name};
    }
    return $self->_plack_req->header($header_name);
}

sub get_method
{
    my ($self, $data) = @_;
    
    my $full_name = $data->{method};
    
    $full_name =~ /^(\S+)\.([^\.]+)$/;
    my ($package, $method) = ($1, $2);
    
    if (!$package || !$method) {
	$self->exception('NoSuchMethod',
			 "'$full_name' is not a valid method. It must"
			 . " contain a package name, followed by a period,"
			 . " followed by a method name.");
    }

    if (!$self->valid_methods->{$method})
    {
	$self->exception('NoSuchMethod',
			 "'$method' is not a valid method in service fba_tools.");
    }
	
    my $inst = $self->instance_dispatch->{$package};
    my $module;
    if ($inst)
    {
	$module = $inst;
    }
    else
    {
	$module = $self->get_module($package);
	if (!$module) {
	    $self->exception('NoSuchMethod',
			     "There is no method package named '$package'.");
	}
	
	Class::MOP::load_class($module);
    }

    if (!$module->can($method)) {
	$self->exception('NoSuchMethod',
			 "There is no method named '$method' in the"
			 . " '$package' package.");
    }
    
    return { module => $module, method => $method, modname => $package };
}

sub handle_input_cli {
    my ($self, $input) = @_;
    my $retval;
    eval {
        #my $input_info = $self->check_input($input);
        #my $input_object = $self->decode_input_to_object($input);
        $self->parser->json->utf8(utf8::is_utf8($input) ? 0 : 1);
        my $input_object = $self->parser->json_to_call($input);
        my $data = $self->input_object_to_data($input_object);
        #$self->fix_data($data, $input_info);
        my $method_info = $self->get_method($data);
        my $method_result = $self->call_method($data, $method_info);
        my $collapsed_result = $self->collapse_result($method_result);
        my $output_object = $self->output_data_to_object($collapsed_result);
        $retval = $self->parser->return_to_json($output_object);
    };
    return $retval if defined $retval;
    # The only way that we can get here is by something throwing an error.
    return $self->handle_error_cli($@);
}

sub handle_error_cli {
    my ($self, $error) = @_;
    my $output;
    eval {
        my %error_params;
        if (ref($error) eq 'HASH') {
            %error_params = %{$error};
            if(defined($error_params{context})) {
                # my @errlines;
                # $errlines[0] = $error_params{message};
                # push @errlines, split("\n", $error_params{data});
                # $self->log($Bio::KBase::Log::ERR, $error_params{context}, \@errlines);
                delete $error_params{context};
            }
        } else {
            unless (blessed $error and $error->isa('RPC::Any::Exception')) {
                $error = RPC::Any::Exception::PerlError->new(message => $error);
            }
            %error_params = (
                message => $error->message,
                code    => $error->code,
            );
        }
        my $json_error;
        if ($self->_last_call) {
            $json_error = $self->_last_call->return_error(%error_params);
        } else {
            $json_error = $self->_default_error(%error_params);
        }
        $output = $self->parser->return_to_json($json_error);
    };
    return $output if $output;
    die "$error\n\nAlso, an error was encountered while trying to send"
        . " this error: $@\n";
}

package fba_tools::fba_toolsServerContext;

use strict;

=head1 NAME

fba_tools::fba_toolsServerContext

head1 DESCRIPTION

A KB RPC context contains information about the invoker of this
service. If it is an authenticated service the authenticated user
record is available via $context->user. The client IP address
is available via $context->client_ip.

=cut

use base 'Class::Accessor';

__PACKAGE__->mk_accessors(qw(user_id client_ip authenticated token
                             module method call_id hostname stderr
                             provenance));

sub new
{
    my($class, $logger, %opts) = @_;
    
    my $self = {
        %opts,
    };
    chomp($self->{hostname} = `hostname`);
    $self->{hostname} ||= 'unknown-host';
    $self->{_logger} = $logger;
    $self->{_debug_levels} = {7 => 1, 8 => 1, 9 => 1,
                              'DEBUG' => 1, 'DEBUG2' => 1, 'DEBUG3' => 1};
    return bless $self, $class;
}

sub _get_user
{
    my ($self) = @_;
    return defined($self->user_id()) ? $self->user_id(): undef; 
}

sub _log
{
    my ($self, $level, $message) = @_;
    $self->{_logger}->log_message($level, $message, $self->_get_user(),
        $self->module(), $self->method(), $self->call_id(),
        $self->client_ip());
}

sub log_err
{
    my ($self, $message) = @_;
    $self->_log($Bio::KBase::Log::ERR, $message);
}

sub log_info
{
    my ($self, $message) = @_;
    $self->_log($Bio::KBase::Log::INFO, $message);
}

sub log_debug
{
    my ($self, $message, $level) = @_;
    if(!defined($level)) {
        $level = 1;
    }
    if($self->{_debug_levels}->{$level}) {
    } else {
        if ($level =~ /\D/ || $level < 1 || $level > 3) {
            die "Invalid log level: $level";
        }
        $level += 6;
    }
    $self->_log($level, $message);
}

sub set_log_level
{
    my ($self, $level) = @_;
    $self->{_logger}->set_log_level($level);
}

sub get_log_level
{
    my ($self) = @_;
    return $self->{_logger}->get_log_level();
}

sub clear_log_level
{
    my ($self) = @_;
    $self->{_logger}->clear_user_log_level();
}

package fba_tools::fba_toolsServerStderrWrapper;

use strict;
use POSIX;
use Time::HiRes 'gettimeofday';

sub new
{
    my($class, $ctx, $get_time) = @_;
    my $self = {
	get_time => $get_time,
    };
    my $dest = $ENV{KBRPC_ERROR_DEST} if exists $ENV{KBRPC_ERROR_DEST};
    my $tag = $ENV{KBRPC_TAG} if exists $ENV{KBRPC_TAG};
    my ($t, $us) = gettimeofday();
    $us = sprintf("%06d", $us);
    my $ts = strftime("%Y-%m-%dT%H:%M:%S.${us}Z", gmtime $t);

    my $name = join(".", $ctx->module, $ctx->method, $ctx->hostname, $ts);

    if ($dest && $dest =~ m,^/,)
    {
	#
	# File destination
	#
	my $fh;

	if ($tag)
	{
	    $tag =~ s,/,_,g;
	    $dest = "$dest/$tag";
	    if (! -d $dest)
	    {
		mkdir($dest);
	    }
	}
	if (open($fh, ">", "$dest/$name"))
	{
	    $self->{file} = "$dest/$name";
	    $self->{dest} = $fh;
	}
	else
	{
	    warn "Cannot open log file $dest/$name: $!";
	}
    }
    else
    {
	#
	# Log to string.
	#
	my $stderr;
	$self->{dest} = \$stderr;
    }
    
    bless $self, $class;

    for my $e (sort { $a cmp $b } keys %ENV)
    {
	$self->log_cmd($e, $ENV{$e});
    }
    return $self;
}

sub redirect
{
    my($self) = @_;
    if ($self->{dest})
    {
	return("2>", $self->{dest});
    }
    else
    {
	return ();
    }
}

sub redirect_both
{
    my($self) = @_;
    if ($self->{dest})
    {
	return(">&", $self->{dest});
    }
    else
    {
	return ();
    }
}

sub timestamp
{
    my($self) = @_;
    my ($t, $us) = $self->{get_time}->();
    $us = sprintf("%06d", $us);
    my $ts = strftime("%Y-%m-%dT%H:%M:%S.${us}Z", gmtime $t);
    return $ts;
}

sub log
{
    my($self, $str) = @_;
    my $d = $self->{dest};
    my $ts = $self->timestamp();
    if (ref($d) eq 'SCALAR')
    {
	$$d .= "[$ts] " . $str . "\n";
	return 1;
    }
    elsif ($d)
    {
	print $d "[$ts] " . $str . "\n";
	return 1;
    }
    return 0;
}

sub log_cmd
{
    my($self, @cmd) = @_;
    my $d = $self->{dest};
    my $str;
    my $ts = $self->timestamp();
    if (ref($cmd[0]))
    {
	$str = join(" ", @{$cmd[0]});
    }
    else
    {
	$str = join(" ", @cmd);
    }
    if (ref($d) eq 'SCALAR')
    {
	$$d .= "[$ts] " . $str . "\n";
    }
    elsif ($d)
    {
	print $d "[$ts] " . $str . "\n";
    }
	 
}

sub dest
{
    my($self) = @_;
    return $self->{dest};
}

sub text_value
{
    my($self) = @_;
    if (ref($self->{dest}) eq 'SCALAR')
    {
	my $r = $self->{dest};
	return $$r;
    }
    else
    {
	return $self->{file};
    }
}


unless (caller) {
    my($input_file,$output_file,$token) = @ARGV;
    my @dispatch;
    {
        use fba_tools::fba_toolsImpl;
        my $obj = fba_tools::fba_toolsImpl->new;
        push(@dispatch, 'fba_tools' => $obj);
    }
    my %headers = (
        "Authorization" => $token,
        "CLI" => "1"
    );
    my $server = fba_tools::fba_toolsServer->new(
        instance_dispatch => { @dispatch },
        allow_get => 0, 
        local_headers => \%headers);
    open(my $fih, '<', $input_file) or die $!;
    my $input = '';
    while (<$fih>) {
        $input .= $_;
    }
    close $fih;
    my $output = $server->handle_input_cli($input);
    open(my $foh, '>', $output_file) or die "Could not open file '$output_file' $!";
    print $foh $output;
    close $foh;
}

1;
